# Cobble

Ghost client for Lunar Client (Minecraft 1.8.9) via JNI injection.

## Features

- **Combat**: AutoClicker, AimAssist, Velocity, Reach, AutoRefill
- **Visual**: NameTags, ESP, Tracers, Notifications, ArrayList
- **Utility**: NoJumpDelay
- **UI**: Custom ImGui overlay with modular settings

## Quick start

```powershell
# Clone with submodule (includes Themida binaries)
git clone --recursive https://github.com/SyraTechnology/cobble
cd cobble

# Configuration
copy cobble.json.example cobble.json
# Edit cobble.json: set host, payload_key, public_key_hex
#   payload_key MUST match $PAYLOAD_KEY in the cobble-server .env
#   public_key_hex MUST be derived from cobble-server's secrets/private.key

# Build (default = dev: no Themida, file tracer enabled)
.\build.ps1
.\build.ps1 -Build

# Pack: Themida + signed manifest. Use this for any binary you ship.
.\build.ps1 -Pack

# Pack + scp to VPS
.\build.ps1 -Pack -Deploy
```

If you cloned without `--recursive`:

```powershell
git submodule update --init themida
```

Output:
- Dev build: `out/bin/Dev/{Cobble.dll, CobbleLoader.exe}`
- Pack build: `out/bin/Pack/{Cobble.dll, CobbleLoader.exe}`

There is no separate Debug build — every build is Release; `-Pack` controls whether Themida wraps the binary and whether the manifest is signed for distribution. The two modes use different output dirs so they don't clobber each other.

## Repos

| Repo | Stack | Purpose |
|---|---|---|
| `cobble`         | C++20 / MSBuild       | this repo: client DLL + injector |
| `cobble-themida` | Themida SDK + project | git submodule at `themida/` |
| `cobble-server`  | Go 1.22 + PostgreSQL  | license API, payload key, telemetry, signed updates |
| `cobble-web`     | Next.js 16 / React 19 | public site, dashboard, payments |
| `cobble-bot`     | Go + discordgo        | Discord ticket bot |

## Layout

```
.
├── Cobble.sln
├── Cobble.vcxproj
├── build.ps1                # main build script
├── generate_config.ps1      # cobble.json -> generated/*.h
├── cobble.json              # secrets + version (gitignored)
├── cobble.json.example      # template for fresh clones
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT.md
│   └── UPDATE_WORKFLOW.md
├── src/                     client DLL (modules, JNI, hooks, overlay, security)
├── loader/                  injector (D3D11 + ImGui, security, updater)
├── infra/ext/               vendored: imgui, minhook, jni, stb
├── generated/               built per compile from cobble.json (gitignored)
├── tools/                   build helpers
└── themida/                 -> SyraTechnology/cobble-themida (submodule)
```

## Documentation

- [CLAUDE.md](CLAUDE.md) — current architecture and constraints
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — system design and threading model
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — module template and conventions
- [docs/UPDATE_WORKFLOW.md](docs/UPDATE_WORKFLOW.md) — release pipeline, signed manifests

## Requirements

- Visual Studio 2022 with C++ workload (Community, Professional, or Enterprise)
- Go 1.22+ (required for `-Pack` manifest signing — `go run` is called inline)
- Windows 10/11 x64
- `infra/keys/private.key` — Ed25519 seed from the VPS (`/opt/cobble-server/secrets/private.key`). Get from team lead. Only needed for `-Pack`.

## Production build

```powershell
# cobble.json must already have payload_key + public_key_hex matching the server
.\build.ps1 -Pack            # builds + Themida + signs the update manifest
.\build.ps1 -Pack -Deploy    # also scp's loader+DLL to the VPS
```

## External VMProtect flow

```powershell
# Step 1: key holder builds production binaries without Themida
.\build.ps1 -Pack -Protector None

# Step 2: protect out/bin/Pack/Cobble.dll and CobbleLoader.exe with VMProtect
#         and place the protected files in out/vmprotect/

# Step 3: key holder signs the protected files and deploys them
.\build.ps1 -SignExisting -ExistingReleaseDir out\vmprotect -Deploy
```

## License

Private.
