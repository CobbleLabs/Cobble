# CLAUDE.md

Guidance for Claude Code working in this repo.

## What this repo is

Just the **C++ client**: loader (D3D11 + ImGui injector) and the injectable
DLL (JNI / OpenGL2 overlay running inside `javaw.exe`).

Everything else has been split out:

| Repo | What | Where |
|---|---|---|
| `cobble-server`  | Go 1.22 + Postgres license API | `github.com/SyraTechnology/cobble-server` |
| `cobble-web`     | Next.js 16 + React 19 dashboard / landing | `github.com/SyraTechnology/cobble-web` |
| `cobble-bot`     | Discord ticket bot (Go) | `github.com/SyraTechnology/cobble-bot` |
| `cobble-themida` | Themida SDK + project file | `github.com/SyraTechnology/cobble-themida` (submodule at `themida/`) |

VPS: `100.27.226.125` — runs `cobble-server` + `cobble-web` behind Cloudflare → nginx → Docker Compose.

## Single source of truth

`cobble.json` is the **only** place server URL, version, payload key and
public key live. `build.ps1` runs `generate_config.ps1` first which produces
four obfuscated headers in `generated/` (gitignored):

- `generated/server_config.h` — XOR-encoded host (narrow + wide), URL helpers, `BUILD_CANARY`
- `generated/keys.h` — XOR-encoded Ed25519 public key with random per-build mask
- `generated/payload_key.h` — XOR-encoded AES-256 payload key (loader only)
- `generated/version.h` — `#define COBBLE_VERSION` from `cobble.json`

Per-build random masks change every compile (polymorphic, anti-pattern-matching).
**Do not add a hardcoded host or key anywhere — always go through `cobble_config::*`.**

`cobble.json` is gitignored; `cobble.json.example` is the template. Bump version
in `cobble.json`, never in code.

## Build

```powershell
.\build.ps1 -Release           # Release
.\build.ps1                    # Debug
.\build.ps1 -Clean             # Wipe out/, generated/, .enc, hash headers
.\build.ps1 -PrepareRelease    # Release + sign manifest with Ed25519
.\build.ps1 -PrepareRelease -Deploy   # Build + scp to VPS
.\build.ps1 -SkipDLL           # Loader-only rebuild
```

Order of operations inside `build.ps1`:

1. `generate_config.ps1` — produce `generated/*.h` from `cobble.json`
2. Compile `Cobble.dll`
3. AES-256-CBC encrypt → `loader/res/Cobble.enc`
4. Generate `loader/src/util/dll_hash.h` (SHA256 of DLL)
5. Build `CobbleLoader.exe` (Pass 1)
6. Hash the loader in 3 chunks → `loader/src/security/integrity_gen.hpp`
7. Rebuild `CobbleLoader.exe` (Pass 2, self-aware)

`PAYLOAD_KEY` is read from `cobble.json` automatically; the env var override is
still respected for CI but no longer required. The same key must match the
server's `.env`.

**Release build is intentionally un-optimized.** Themida (TIGER64/SHARK64)
post-processes the binary; don't "fix" `Optimization=Disabled`.

MSBuild path is hardcoded in `build.ps1` — adjust if VS lives elsewhere.

## Code Quality

```powershell
.\tools\code_quality.ps1 -Format       # clang-format in place
.\tools\code_quality.ps1 -Lint         # clang-tidy
.\tools\code_quality.ps1 -OrphanFiles  # .cpp on disk missing from .vcxproj
.\tools\code_quality.ps1 -All
```

C++20, x64. DLL = MultiByte. Loader = Unicode.

## Auto-generated files (do not edit)

- `generated/*.h` — regenerated every compile from `cobble.json`
- `loader/src/util/dll_hash.h` — SHA256 of `Cobble.dll`
- `loader/src/security/integrity_gen.hpp` — chunked SHA256 of `CobbleLoader.exe`
- `loader/res/Cobble.enc` — AES-256-CBC encrypted DLL payload

## Architecture: critical constraints

These aren't obvious from any single file but will bite if violated.

### Module system

- **All modules are heap-allocated.** Static module instances crash on inject. `cobble::context` (`src/cobble.cpp`) is sole owner and deleter. `ModuleManager` only iterates non-owning pointers.
- Adding a module is a **7-step checklist**: header + impl + register in `cobble.cpp::initialize()` + delete in `shutdown()` + entry in `widgets.cpp` tab list + entry in `menu.cpp::getModuleForSubtab()` + entries in **both** `config.cpp::applyConfig()` and `buildConfigFromModules()` + entry in `Cobble.vcxproj`. Missing the vcxproj entry compiles fine but never links the module. Missing config serialization means settings silently don't persist. Full template in `docs/DEVELOPMENT.md`.
- Modules subclass `Module` (`src/module/module.h`) and self-render via `onRenderMenu()`. **Never put module-specific UI in `menu.cpp`** — it only routes tab/subtab to a `Module*` via `getModuleForSubtab()`.
- `Module::locked = true` is gated behind `experimentalUnlocked` (typed unlock code).
- `ModuleManager::update()` only runs when **Minecraft has the foreground window**. Don't rely on background ticks.
- `Module::shouldUpdate()` returns `sdk::internal::isOperational()` — if JNI state is poisoned, modules silently stop updating. **Don't add user-visible feedback to this path.**
- AutoRefill has a special **passive mode** path: when `settings.onInventoryOpen` is set, `onUpdate()` runs even with `enabled=false`.

### Threading

| Thread | Allowed | Cadence |
|---|---|---|
| Main / JNI | JNI calls, `onUpdate()`, `RenderData::Update()`, `VisualCache::Update()` | session 1s, player 2ms, modules 2ms, render 4ms, visual cache 4ms |
| OpenGL/render | ImGui calls **only** | per frame, hooked via `wglSwapBuffers` |
| Heartbeat | HTTPS to `/api/heartbeat`, `DllIntegrity::Check()` | 120-180s with jitter |
| Detached | Config sync, debounced server pushes | 400ms debounce |

**Calling JNI from the render thread will eventually crash.** Cache values in `onUpdate()`/`VisualCache::Update()`, read them in `onRenderMenu()`/`onRender()`. The `VisualCache` (`src/sdk/player/visual_cache.h`) is the canonical bridge.

### UI

- Always use `ui::` wrappers from `src/ui/widgets/widgets.hpp` (`ui::Checkbox`, `ui::SliderFloat`, `ui::Combo`, `ui::two_column`, `ui::s()`). Raw `ImGui::` widgets break the menu fade.
- Overlay (`src/ui/core/overlay.cpp`) is intentionally cautious about reinit. If you see `GL ERROR 1282`, the target context is unstable; don't paper over it.
- DLL overlay = OpenGL2. Loader UI = D3D11. Different ImGui impl files.

### Security handshake (loader ↔ DLL)

The DLL refuses to run unless injected through this handshake:

1. Loader → server: `/api/verify` or `/api/loader/login` → server returns `injection_secret` (5 min TTL), `session_token`, `sync_token`, `handshake_sig` (Ed25519).
2. Loader → server: `/api/loader/payload-key` → AES key matching build-time `PAYLOAD_KEY`.
3. Loader writes `InjectionData` to per-PID shared memory `Local\Cobble_<PID>` with a current-user-only DACL.
4. Loader injects via `SecureMapper::SecureManualMap` (NT syscalls; no `LoadLibrary`, no `WriteProcessMemory`, no disk write).
5. DLL `SessionValidator::ValidateInjection()` reads the shared memory, verifies signature `COBBLE_INJ_V1`, expiry, and the **Ed25519 signature over `COBBLE_HANDSHAKE|<user>|<hwid>|<timestamp>|<secret>`** with `cobble_keys::GetPublicKey()`.
6. `LoaderVerify::verifyOrDie()` confirms the DLL is **not** in the PEB module list (would indicate `LoadLibrary`).
7. `LoaderValidator::RunStartupValidation()` runs Phase 4 checks: parent process not a debugger, loader process exists, no debugger attached, injection within 15min of process start.
8. On failure: `memset(hModule, 0xCC, 0x1000)` + `ExitProcess(0)`. **No "security" string anywhere.**

### Runtime degradation (`src/util/state.hpp`)

After validation, the DLL keeps `g_stateFlags` and `JniCache` checksum. Triggers that flip "corrupted":

- 3 consecutive heartbeat failures
- Server returns `reason: "sync_error"` on heartbeat
- Dead-man switch: `MAX_DRIFT_MS = 320000` (~5.3 min) without `syncTick()`
- `JniCache` checksum mismatch

Once corrupted, `attemptRecovery()` nulls cached `jclass`/`jmethodID`s. Next JNI call deref nulls and crashes. Looks like memory corruption, not security. **Do not add visible feedback.**

### Heartbeat & token rotation

- DLL POSTs JSON `{session_token, hwid, username, version, sync_hash}` to `/api/heartbeat`.
- `sync_hash = SHA256(sync_token + hwid + (unix/60))` with ±1 minute bucket tolerance.
- Server rotates both `session_token` and `sync_token` on every successful heartbeat. Stolen tokens become invalid after one rotation.
- `DllIntegrity::Check()` (SHA256 of `.text` with retry) runs on the heartbeat thread, not the main loop.
- **All client → server requests are JSON.** Cloudflare WAF rejects form-encoded.

### Honeypot (`src/security/honeypot.hpp`)

Three intentionally easy-to-find fake Discord webhook URLs in the binary. They are **decoys**. Real telemetry goes to `/api/telemetry` (server forwards to Discord). If you see `FAKE_WEBHOOK_*`, do not "fix" them.

### JNI / packet hooks

- **JVMTI bytecode patching**, not native method hooks.
- `apollo/apollo_packet_observer.*` — incoming-only observer for Lunar's Apollo team-color packets.
- `apollo/apollo_handshake.*` — pending Apollo handshakes ticked from the main loop.
- `noitemrelease/noitemrelease_gate.*` — outgoing gate for `C07PacketPlayerDigging.RELEASE_USE_ITEM`.
- `blink/blink_gate.*` and `backtrack/backtrack_gate.*` — outgoing gates.
- `LunarPacketListener` (`src/sdk/network/`) — separate path for `LCPacketNametagsUpdate`/`Override`.

No tick/drain loop; everything fires on incoming JVMTI hooks.

### Winsock hooks (`src/hooks/winsock_hooks.cpp`)

`Blink` module. MinHook on `WS2_32!WSASend`/`WSARecv` and **`NtDeviceIoControlFile` AFD_SEND** (the Java NIO path).

### `JniCache` (`src/sdk/jni/jni_cache.h`)

All `jclass`/`jmethodID`/`jfieldID` cached as GlobalRefs once in `Init()`. **Never `FindClass`/`GetMethodID` per-tick.** When poisoned, every consumer dereferences nullptr — that is the intended kill mechanism.

## Config persistence

`ConfigManager` (`src/config/config.cpp`):

- Loads `%APPDATA%/Cobble/config.json` synchronously, applies, then async-fetches `/api/config` and merges (server wins for `flags`/`hud`/`modules`).
- Saves debounced 400ms → `/api/config/save` (JSON body).
- One-time session token in `%APPDATA%/Cobble/.session` (loader writes, DLL reads + deletes).
- User configs under `%APPDATA%/Cobble/configs/*.json`. Names go through `sanitize_user_config_name()`.

**Adding a module setting requires entries in BOTH `applyConfig()` AND `buildConfigFromModules()`** in `config.cpp`, or settings won't persist.

## Loader self-update

`CobbleLoader.exe` doubles as its own self-replacer. With `--update-pid <PID> --update-old <path> --update-new <path>` it skips its normal flow, waits for the old PID to exit, swaps the binary, relaunches with `CREATE_NEW_CONSOLE`. See `docs/UPDATE_WORKFLOW.md`.

## Diagnostics (Debug only)

`src/util/diagnostics.hpp`: 3s freeze watchdog, `DIAG_SCOPE("name")` markers. **`DIAG_SCOPE` is a no-op in Release.** `Diagnostics::g_currentOperation` is set per-module in `module_manager.cpp`.

## Server endpoints used by loader/DLL

All POST + JSON.

| Endpoint | Caller |
|---|---|
| `/api/loader/login` | loader (first login) |
| `/api/verify` | loader (HWID-bound) |
| `/api/loader/payload-key` | loader (after auth) |
| `/api/dll/validate` | DLL (startup) |
| `/api/heartbeat` | DLL (every 2-3 min) |
| `/api/telemetry` | loader + DLL (security events) |
| `/api/internal/audit` | DLL (detailed audit) |
| `/api/config`, `/api/config/save` | DLL (settings sync) |
| `/api/update/manifest`, `/api/update/loader`, `/api/update/dll` | loader (GET) |

## More

- `docs/ARCHITECTURE.md` — execution flow, security phase diagrams, threading
- `docs/DEVELOPMENT.md` — module template, UI conventions, anti-patterns
- `docs/UPDATE_WORKFLOW.md` — release pipeline, manifest format, downgrade detection
- `cobble-server/DEPLOY.md` — VPS setup (Cloudflare + nginx + Docker)
