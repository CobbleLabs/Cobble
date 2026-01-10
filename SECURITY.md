# Security Incident Report & Hardening

**Date**: January 9-10, 2026  

---

## Incident Summary

On January 9, 2026, we discovered that the Cobble client had been cracked and distributed publicly. The attacker was able to:

1. Extract the encrypted DLL payload from the loader
2. Decrypt it using the hardcoded key derivation (trivial XOR obfuscation)
3. Load the DLL directly with `LoadLibrary()` bypassing all loader protections
4. Use the client without a valid license

The root cause was that **all security checks were client-side only**, meaning once the DLL was extracted, it could run independently without any server validation.

---

## Attack Vector Analysis

### What Failed

| Protection | Status | Why It Failed |
|------------|--------|---------------|
| DLL Encryption | ❌ Broken | Key derived from hardcoded seed with XOR - easily reversible |
| Loader Verification | ❌ Bypassed | Only checked if loaded via manual mapping - `LoadLibrary` worked |
| Session Token | ❌ Ignored | Token was optional; DLL worked without it |
| Integrity Check | ❌ Useless | DLL computed its own "golden hash" at startup |

### The Cracking Script

```python
# This is what the attacker used (simplified)
PAYLOAD_KEY_SEED = b"LoaderKey2026CobbleSecurePayload"

def derive_key():
    key = bytearray(32)
    for i in range(32):
        key[i] = PAYLOAD_KEY_SEED[i] ^ ((i * 0x37) & 0xFF) ^ 0xAB
    return bytes(key)

# Decrypt with AES-256-CBC using the derived key
# Result: fully functional Cobble.dll
```

---

## Security Hardening Phases

We implemented a multi-layered defense to prevent this from happening again.

### Phase 1: Server-Gated DLL Execution ✅

**Problem**: DLL ran without any server validation.

**Solution**: Server now issues a unique `injection_secret` per login that the DLL requires to operate.

```
┌─────────────┐      1. Verify/Login       ┌─────────────┐
│   Loader    │ ─────────────────────────► │   Server    │
│             │ ◄───────────────────────── │             │
└──────┬──────┘   injection_secret (32B)   └─────────────┘
       │
       │ 2. Write to shared memory
       ▼
┌─────────────┐
│  DLL reads  │─── Validates secret ───► Operates normally
│  secret     │                          OR crashes silently
└─────────────┘
```

**Files Changed**:
- `license-server/internal/handlers/verify.go` - Generate `injection_secret`
- `license-server/internal/handlers/loader.go` - Generate `injection_secret`
- `loader/main.cpp` - Write `InjectionData` to shared memory
- `src/util/session_validator.hpp` - Read and validate secret

**Impact**: Extracted DLLs no longer work - they require a fresh server-issued secret.

---

### Phase 2: Server-Provided Payload Key ✅

**Problem**: Encryption key was derived from hardcoded bytes in the binary.

**Solution**: The AES-256 key now comes from the server at runtime.

```
┌─────────────┐      1. Verify License     ┌─────────────┐
│   Loader    │ ─────────────────────────► │   Server    │
│             │ ◄───────────────────────── │             │
└──────┬──────┘      2. Session Token      └──────┬──────┘
       │                                          │
       │ 3. Request Payload Key                   │
       │ ─────────────────────────────────────────►
       │ ◄─────────────────────────────────────────
       │         PAYLOAD_KEY (64 hex chars)       │
       ▼                                          
┌─────────────┐
│  Decrypt    │─── AES-256-CBC with server key
│  Cobble.enc │
└─────────────┘
```

**Files Changed**:
- `license-server/internal/handlers/payload.go` - New `/api/loader/payload-key` endpoint
- `license-server/internal/config/config.go` - `PAYLOAD_KEY` env var
- `loader/src/license/license.cpp` - `FetchPayloadKey()` function
- `loader/src/core/payload_crypto.hpp` - `DecryptPayloadWithKey()`
- `tools/encrypt_payload.ps1` - `-PayloadKey` parameter for secure builds

**Configuration**:
```bash
# Server (.env)
PAYLOAD_KEY=<64-char-hex-key>

# Build
$env:PAYLOAD_KEY = "<same-key>"
.\build.ps1 -Release
```

**Impact**: The decryption script (`decrypt_dll_demo.py`) no longer works with secure builds.

---

### Phase 3: Signed Handshake Verification ✅

**Problem**: An attacker could create a fake loader that writes valid-looking data to shared memory.

**Solution**: Server signs the handshake data with Ed25519. DLL verifies the signature.

```
┌─────────────┐                           ┌─────────────┐
│   Server    │ Signs with private key:   │             │
│             │ "COBBLE_HANDSHAKE|user|   │   Loader    │
│             │  hwid|timestamp|secret"   │             │
└──────┬──────┘                           └──────┬──────┘
       │                                         │
       │ handshake_sig (Base64 Ed25519)          │
       └─────────────────────────────────────────┤
                                                 │
                      Writes to shared memory    │
                                                 ▼
                                          ┌─────────────┐
                                          │     DLL     │
                                          │  Verifies   │
                                          │  signature  │
                                          │  with       │
                                          │  PUBLIC KEY │
                                          └─────────────┘
```

**Files Changed**:
- `license-server/internal/handlers/verify.go` - Generate `handshake_sig`
- `license-server/internal/handlers/loader.go` - Generate `handshake_sig`
- `loader/main.cpp` - `InjectionData.handshakeSig` field
- `loader/src/license/license.cpp` - Parse and store signature
- `src/util/session_validator.hpp` - Ed25519 verification with embedded public key

**Impact**: Even if an attacker creates a fake loader with shared memory access, they cannot forge valid signatures without the server's private key.

---

### Phase 4: Runtime Server Validation ✅

**Problem**: Once the DLL passes initial validation, it runs indefinitely without re-checking with server.

**Solution**: Background heartbeat thread pings server every 2-3 minutes.

```
┌─────────────┐      Every 2-3 min      ┌─────────────┐
│     DLL     │ ──────────────────────► │   Server    │
│  Heartbeat  │       POST /heartbeat   │   Checks:   │
│   Thread    │ ◄────────────────────── │   - banned? │
└──────┬──────┘      {ok:true/false}    │   - expired?│
       │                                │   - revoked?│
       │ If ok:false                    └─────────────┘
       ▼
 ExitProcess(0)  ◄── Kill switch
```

**Files Changed**:
- `license-server/internal/handlers/heartbeat.go` - New `/api/heartbeat` endpoint
- `license-server/internal/database/db.go` - `UpdateUserLastSeen()`
- `src/util/session_validator.hpp` - Heartbeat thread implementation
- `loader/main.cpp` - Pass `sessionToken` via shared memory

**Configuration**:
- Base interval: 120 seconds
- Jitter: 0-60 seconds random
- Max failures: 3 before kill
- Initial delay: 30 seconds

**Impact**: Global kill switch - stop server = all clients die. Ban user = instant termination.

---

### Phase 5: Additional Hardening (Planned)

- Remove all `MessageBoxA` debug strings in release builds
- Store session tokens with DPAPI (encrypted at rest)
- Add TLS certificate pinning for MITM protection
- Obfuscate public key in DLL binary

---

## Current Security Status

| Layer | Protection | Status | Attack Mitigated |
|-------|------------|--------|------------------|
| 1 | DLL Encryption (Server Key) | ✅ | Binary extraction |
| 2 | Injection Secret | ✅ | DLL without legitimate loader |
| 3 | Signed Handshake | ✅ | Fake loader with shared memory |
| 4 | Runtime Heartbeat | ✅ | Offline session replay, instant ban |
| 5 | Certificate Pinning | ❌ Pending | MITM attacks |

---

## Deployment Checklist

When deploying a new version:

1. **Generate payload key** (one time):
   ```bash
   openssl rand -hex 32
   ```

2. **Configure server**:
   ```bash
   echo 'PAYLOAD_KEY=<key>' >> /opt/cobble/.env
   systemctl restart cobble
   ```

3. **Build with secure key**:
   ```powershell
   $env:PAYLOAD_KEY = "<key>"
   .\build.ps1 -Release
   ```

4. **Verify in server logs**:
   ```
   VERIFY: OK
   TOKEN: Created session token
   PAYLOAD: Key delivered
   ```

---

## Lessons Learned

1. **Never trust the client** - All security gates must involve the server
2. **Encryption is not obfuscation** - A hardcoded key is no key at all
3. **Defense in depth** - Multiple layers catch what single layers miss
4. **Cryptographic signatures** - Only the server private key can create valid tokens
5. **Fail closed** - DLL refuses to run rather than running unverified

---

## Contact

For security concerns, contact the development team directly.
