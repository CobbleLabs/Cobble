# Update & Release Workflow

## Branches

| Branch | Role |
|--------|------|
| `testing` | Daily work. Default branch. Features land here. |
| `main` | Production. Merge here only when you are shipping. |

**Feature:** branch off `testing`, merge back into `testing`.

**Release:** merge `testing` → `main`, bump `cobble.json` `version`, deploy from `main`. `build.ps1 -Pack -Deploy` tags `v<version>` on that commit.

**Hotfix:** branch off `main` (or the `v*` tag), fix, merge into `main`, deploy, then merge `main` into `testing` so the fix is not lost.

Do not deploy from `testing`. Unfinished work on that branch would ship.

---

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
| Server Endpoints | `cobble-server/internal/handlers/update.go` | Serve manifest and binaries |

---

## Initial Setup (One Time Only)

```powershell
.\tools\generate_keys.ps1
```

This automatically:
- Generates Ed25519 keypair in `infra/keys/`
- Updates `cobble.json` with the new public key (or copy it manually)
- Creates the private key for signing releases (`infra/keys/private.key`)

---

## Deploying a New Version

### 1. Update version

Edit `cobble.json`:
```json
{ "version": "1.0.15", ... }
```

`generate_config.ps1` produces `generated/version.h` from this on every build.

`cobble.json` contains deployment credentials and other private configuration.
It is ignored by Git and must never be committed.

### 2. Owner-only build + protect + sign + deploy

```powershell
.\build.ps1
# Select option 3: Build + Pack + Deploy
```

The script will ask:
```
Is this release MANDATORY? (y/N)
```
Answer `y` if you want to force all users to update immediately.

Option 3:

- Builds the in-repo VMProtect console from the linked source checkout.
- Builds and protects `Cobble.dll` with the Ultra mutation + virtualization
  profile.
- Encrypts that protected DLL into the loader payload.
- Builds and protects `CobbleLoader.exe` with the same Ultra profile.
- Generates and signs `manifest.json` and `manifest.sig`.
- Prepares a temporary local deployment bundle and uploads all four artifacts
  directly to the VPS configured in `cobble.json`.
- Does not create or publish a GitHub Release.
- Generates release notes from the nearest reachable `v*` tag to `HEAD`.
- Publishes the same canonical update to the website and Discord only after SCP
  succeeds.
- Tags and pushes `v<version>` only after the update publication succeeds.

Only the owner/operator with the signing key and VPS credentials should run
option 3. The tag is the production commit: hotfix from it, not from `testing`.

### Automated release announcements

Deploy the updated `cobble-server-main` and `cobble-web` before enabling this
flow. Migration 27 adds structured changelog fields and Discord delivery state.

Configure these secrets in the backend/VPS environment:

```text
RELEASE_PUBLISH_TOKEN=<long-random-machine-token>
DISCORD_UPDATES_WEBHOOK=<rotated-updates-webhook-url>
```

Configure the build environment with the same machine token. The optional API
URL is only needed when the backend is not hosted at `https://api.cobble.vip`:

```powershell
$env:COBBLE_RELEASE_TOKEN = "<same-machine-token>"
$env:COBBLE_RELEASE_API_URL = "https://api.cobble.vip"
```

Never store either secret in Git, `cobble.json`, generated headers, a
`NEXT_PUBLIC_*` variable, or a command committed to the repository. The Discord
webhook remains backend-only. Rotate any webhook URL that has been pasted into
chat, logs, or another non-secret channel.

Before deploying, commit the release changes, merge them into `main`, bump
`cobble.json` `version`, and confirm the previous production `v*` tag is
reachable. Preview the generated payload without build, SCP, HTTP, or Discord
calls:

```powershell
.\build.ps1 -ReleaseNotesOnly
# Override tag discovery only when intentionally recovering history:
.\build.ps1 -ReleaseNotesOnly -BaseRef v1.7.0
```

The generator uses changed paths to assign `Movement`, `Utility`, `Network`,
`Combat`, `Settings`, or `Gui`; commit subjects become descriptions and commit
bodies become details. Write user-facing commit messages. The JSON and Markdown
preview are written under `out/release-preview/` and validated against the
backend and Discord's 2,000-character limit.

Run the production release only from a clean worktree:

```powershell
.\build.ps1 -Pack -Deploy
```

The release order is strict:

1. Generate and validate notes from the previous production tag to `HEAD`.
2. Build, protect, sign, and prepare the four release artifacts.
3. Upload the artifacts to the VPS with SCP.
4. POST the structured payload to `/api/release/publish` using
   `X-Release-Token`.
5. In one database transaction, create the website announcement and changelog.
6. Send the backend-generated Markdown to Discord with `@everyone` enabled.
7. Tag and push the deployed commit as `v<version>`.

The publish endpoint is idempotent by version plus canonical Markdown. Repeating
the same request returns the existing update; using the same version with
different content returns a conflict.

#### Recovery

- **SCP failed:** nothing was announced or tagged. Fix SSH/VPS access and rerun
  the full `-Pack -Deploy` command.
- **SCP succeeded, but the backend request failed before saving the update:**
  do not change commits or version; fix backend/token configuration and run
  `.\build.ps1 -PublishOnly`. This does not rebuild or upload artifacts.
- **Website rows exist, but Discord failed:** do not republish the release.
  Retry Discord through authenticated admin endpoint
  `POST /api/admin/changelogs/<id>/retry-discord`; it sends the stored canonical
  Markdown and becomes a no-op after status `sent`.
- **Same version, different Markdown conflict:** stop and reconcile the deployed
  commit, version, and generated notes. Do not overwrite the existing update.
- **Emergency dirty release:** `-AllowDirtyRelease` includes reviewed local
  source changes in the notes but deliberately skips the Git tag because the
  deployed binaries do not correspond exactly to `HEAD`.
- **Intentional announcement bypass:** `-SkipUpdateAnnouncement` uploads without
  publishing. Use only as a break-glass option, then recover with
  `-PublishOnly` before changing the release commit.

### Why the VPS receives an EXE and a DLL

The website can distribute only `CobbleLoader.exe` to end users because the
loader already contains the encrypted protected DLL payload and maps it after
successful authentication. The protected `Cobble.dll` is also uploaded as a
separate server artifact because the current signed manifest and update API
track both files. Do not remove that separate DLL until the server contract,
manifest fields, and `/api/update/dll` endpoint are changed together.

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
