# Update & Release Workflow

## Overview

Cobble uses an **integrated autoupdater** that allows the loader to update itself automatically. The system includes:

- **Ed25519 signature verification** for manifest authenticity
- **SHA256 + size verification** for downloaded binaries
- **Self-replacing update mode** integrated into the loader

---

## How Autoupdate Works

### Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         USER RUNS OLD LOADER                             │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  1. Check /api/update/manifest                                           │
│     └── Compare version: "1.0.5" < "1.0.10" → Update available           │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  2. Prompt user (optional) or auto-download (mandatory)                  │
│     └── "Update v1.0.10 available! Press Y to update..."                │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  3. Download from /api/update/loader?hwid=xxx                            │
│     └── Save to %TEMP%\cb_{guid}\CobbleLoader.exe                        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  4. Verify: SHA256 hash + file size from manifest                        │
│     └── If mismatch → Abort, cleanup temp                                │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  5. Launch NEW loader with update mode args:                             │
│     └── NewLoader.exe --update-pid 1234 --update-old "..." --update-new "..."│
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  6. OLD loader exits (return 0)                                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  NEW LOADER (UPDATE MODE) TAKES OVER:                                    │
│  ├── Wait for old process to exit                                        │
│  ├── Backup: old.exe → old.exe.old                                       │
│  ├── Move: new.exe → old.exe location                                    │
│  ├── Launch: updated loader (with CREATE_NEW_CONSOLE)                    │
│  └── Cleanup: delete backup                                              │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  UPDATED LOADER RUNS NORMALLY                                            │
│  └── User sees new version, continues to Minecraft                       │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | File | Purpose |
|-----------|------|---------|
| Manifest Check | `loader/src/update/updater.h` | Fetch & verify signed manifest |
| Download & Verify | `loader/src/update/download.h` | Download file, SHA256+size check |
| Update Mode | `loader/main.cpp` | Self-replace logic (integrated stub) |
| Manifest Signing | `tools/sign_manifest.go` | Ed25519 signing |
| Server Endpoints | `license-server/handlers/update.go` | Serve manifest and binaries |

---

## Initial Setup (One Time Only)

```powershell
.\tools\generate_keys.ps1
```

This automatically:
- Generates Ed25519 keypair in `keys/`
- Updates `updater.h` with the public key
- Creates private key for signing releases

---

## Releasing a New Version

### 1. Update version

Edit `loader/src/version.h`:
```cpp
#define COBBLE_VERSION "1.0.15"
```

### 2. Build + Sign + Set Mandatory

```powershell
.\build.ps1 -PrepareRelease
```

The script will ask:
```
Is this release MANDATORY? (y/N)
```
Answer `y` if you want to force all users to update immediately.

This:
- Builds Cobble.dll and CobbleLoader.exe
- Generates `manifest.json` (with `mandatory: true/false`)
- Signs the manifest with Ed25519 (creates `manifest.sig`)
- Places everything in `releases/latest/`

### 3. Upload to server

```bash
scp -r releases/latest/* user@server:/opt/cobble/releases/latest/
```

Or via your preferred deployment method.

---

## Manifest Structure

The `manifest.json` file contains:

```json
{
    "version": "1.0.15",
    "min_allowed": "1.0.0",
    "mandatory": false,
    "loader_sha256": "abc123...",
    "loader_size": 123456,
    "dll_sha256": "def456...",
    "dll_size": 789012,
    "notes": ""
}
```

| Field | Description |
|-------|-------------|
| `version` | Latest available version |
| `min_allowed` | Minimum version allowed (older = blocked) |
| `mandatory` | If true, forces update without prompt |
| `loader_sha256` | SHA256 hash of CobbleLoader.exe |
| `loader_size` | Size in bytes of CobbleLoader.exe |
| `dll_sha256` | SHA256 hash of Cobble.dll |
| `dll_size` | Size in bytes of Cobble.dll |
| `notes` | Optional release notes |

---

## Server Endpoints

| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/api/update/manifest` | GET | None | Get signed manifest + signature |
| `/api/update/loader` | GET | HWID | Download CobbleLoader.exe |
| `/api/update/dll` | GET | HWID | Download Cobble.dll |

The loader and DLL endpoints require a valid HWID with an active license.

---

## Discord Bot Commands

| Command | Description |
|---------|-------------|
| `/release` | View current release version and info from manifest.json |

---

## Security

### Manifest Verification

The manifest is signed with Ed25519. The public key is embedded in the loader binary. Any tampering with the manifest will fail signature verification.

### Binary Verification

After downloading, the loader verifies:
1. **SHA256 hash** matches manifest
2. **File size** matches manifest

If either fails, the update is aborted.

### Cross-Drive Support

The update system uses `CopyFile + DeleteFile` when the temp directory and loader are on different drives (e.g., `C:\Users\...\Temp` → `D:\Games\...`).

---

## Troubleshooting

### Update fails silently

Check:
1. Server is reachable and manifest is valid
2. HWID has an active license (required for download)
3. Firewall isn't blocking the connection

### Loader doesn't restart after update

The new loader is launched with `CREATE_NEW_CONSOLE` to ensure it gets its own visible window. If it still doesn't appear:
1. Check Task Manager for CobbleLoader.exe process
2. Check Windows Event Viewer for crash logs

### Wrong version after update

The update replaces the loader at its **original location**. If you run from a different path, you'll see the old version.
