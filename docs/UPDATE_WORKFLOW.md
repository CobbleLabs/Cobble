# Updates and releases

## Branches

| Branch | Role |
|--------|------|
| `testing` | Daily work. Features land here. |
| `main` | Production. Merge here only when shipping. |

Feature: branch off `testing`, merge back into `testing`.

Release: merge `testing` → `main`, bump `cobble.json` `version`, deploy from `main`.

Hotfix: branch off `main` (or the `v*` tag), merge into `main`, deploy, then merge `main` into `testing`.

Do not deploy from `testing`. `build.ps1 -Deploy` warns if HEAD is not `main` and still tags that commit.

---

## What a release is

`.\build.ps1 -Pack -Deploy` (from a Windows machine with VS 2022, Go, `cobble.json`, `infra/keys/private.key`, and an initialized `reprotect/` submodule) produces four files and copies them to the VPS:

```
/opt/cobble-server/releases/latest/CobbleLoader.exe
/opt/cobble-server/releases/latest/Cobble.dll
/opt/cobble-server/releases/latest/manifest.json
/opt/cobble-server/releases/latest/manifest.sig
```

`Cobble.dll` on the VPS is the protected PE, served by `GET /api/update/dll`. The loader’s embedded payload is a separate AES-encrypted blob baked in at pack time (`tools/encrypt_payload.ps1`). Users receive a new payload by downloading a new loader.

`CobbleClicker.exe` is not built or uploaded by this script. If present in `releases/latest/`, cobble-server will serve it from `GET /api/update/clicker` (JWT, no license).

There is no ZIP, no GitHub Release, no Discord publish, and no `/api/release/publish`. Loader filename disguises (`?as=`, icon variants) were removed; the dashboard and the updater both download `CobbleLoader.exe`.

---

## `cobble.json`

| Field | Role |
|-------|------|
| `server.host` | API host baked into the binaries |
| `server.host_secondary` | Optional fallback (e.g. `api2.cobble.vip`) when Cloudflare resets TLS |
| `server.port` / `server.https` | Written into generated headers |
| `version` | Semver `major.minor.patch`. Pack refuses `0.0.0` |
| `payload_key` | 64 hex chars. AES-256 for `encrypt_payload.ps1`. Must equal cobble-server `PAYLOAD_KEY` |
| `public_key_hex` | 64 hex chars. Ed25519 public key the loader uses to verify `manifest.sig` |
| `deploy.vps_host` / `vps_user` / `ssh_key` / `releases_path` | SCP target (example path: `/opt/cobble-server/releases/latest`) |

The matching Ed25519 seed on the VPS is `/opt/cobble-server/secrets/private.key`.

---

## Commands

```powershell
.\build.ps1                          # interactive: 1 Build, 2 Obfuscate, 3 Pack, 4 Pack+Deploy, 5 SignExisting+Deploy, 6 Clean
.\build.ps1 -Build                   # Dev, no protector, no manifest
.\build.ps1 -Build -Obfuscate        # Dev + VMProtect Ultra, unsigned
.\build.ps1 -Pack                    # production binaries + signed manifest, no upload
.\build.ps1 -Pack -Deploy            # pack, scp, tag v<version>
.\build.ps1 -Pack -Deploy -Mandatory # same; mark the update mandatory
.\build.ps1 -SignExisting -Deploy    # sign/deploy already-protected binaries
.\build.ps1 -ReleaseNotesOnly        # preview notes under out/release-preview/; does not ship
.\build.ps1 -Clean
```

`-Release` / `-PrepareRelease` are aliases for `-Pack`. `-Pack -Protector None -Deploy` is rejected (would publish unprotected binaries). `-Pack` with the default `-Protector VMProtect` needs `reprotect/` actually populated (`reprotect/build.ps1`, `VMProtectCon.exe`) and `protection/cobble-ultra.vmp`.

After a successful SCP, `Publish-GitTag` pushes annotated `v<version>`. Existing tags are never moved. A dirty tree, `-AllowDirtyRelease`, or missing git skips the tag (deploy has already happened). Tag failure is a warning.

`generate_config.ps1` runs as part of `build.ps1` and writes XOR-masked headers under `generated/` (gitignored).

---

## Autoupdate

Dev loaders (`-Build`, `-Obfuscate`) skip the check. Packed loaders request `GET /api/update/manifest` (no auth) from the baked host list and pin the first host that answers.

The server returns `{ manifest_b64, sig_b64 }`. It reads the files as raw bytes; it does not parse them into a struct. The loader verifies Ed25519, then reads:

```json
{
  "version": "x.y.z",
  "min_allowed": "x.y.z",
  "mandatory": false,
  "protected": true,
  "loader_sha256": "...",
  "loader_size": 0,
  "dll_sha256": "...",
  "dll_size": 0,
  "notes": ""
}
```

Pack sets `min_allowed` equal to `version`. `protected` is written; the loader does not read it.

If the remote version is newer and the update is **mandatory**, the loader downloads `GET /api/update/loader?hwid=…` into `%TEMP%\cb_{guid}\CobbleLoader.exe`, checks size then SHA256, and launches that file. Optional (non-mandatory) updates are logged and ignored.

`GET /api/update/loader` requires a JWT with an active license or a `?hwid=` that maps to one. It always serves `releases/latest/CobbleLoader.exe`.

---

## Server layout

cobble-server is Docker Compose (postgres + server) behind host nginx. Releases are bind-mounted read-only. `scripts/update.sh` on the VPS checks out `main` and rebuilds the **server** image; it does not build the client.

After `build.ps1 -Pack -Deploy`, no server restart is required for the new loader/DLL. The next manifest fetch sees the new files.
