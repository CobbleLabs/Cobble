# Cobble

Ghost client for Lunar Client (Minecraft 1.8.9) via JNI injection.

## Features

- **Combat**: AutoClicker, AimAssist, Velocity, Reach, AutoRefill
- **Visual**: NameTags, ESP, Tracers, Notifications, ArrayList
- **Utility**: NoJumpDelay
- **UI**: Custom ImGui overlay with modular settings

## Quick start

```powershell
# Clone with submodule
git clone --recursive https://github.com/SyraTechnology/cobble
cd cobble

# Configuration
copy cobble.json.example cobble.json
# Edit cobble.json: set host, payload_key, public_key_hex
#   payload_key MUST match $PAYLOAD_KEY in the cobble-server .env
#   public_key_hex MUST be derived from cobble-server's secrets/private.key

# Themida license (personal, not in git)
copy <your_license>\TMLicenseA1.dat themida\TMLicenseA1.dat

# Build
.\build.ps1 -Release
```

If you cloned without `--recursive`:

```powershell
git submodule update --init themida
```

Output: `out/bin/Release/CobbleLoader.exe`, `out/bin/Release/Cobble.dll`.

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

- Visual Studio 2022 (with MSBuild at the path baked into `build.ps1`)
- Windows 10/11 x64
- Themida (license at `themida/TMLicenseA1.dat`)

## Production build

```powershell
# cobble.json must already have payload_key + public_key_hex matching the server
.\build.ps1 -PrepareRelease           # builds + signs the update manifest
.\build.ps1 -PrepareRelease -Deploy   # also scp's loader+DLL to the VPS
```

## License

Private.
