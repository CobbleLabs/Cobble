# Cobble

Ghost client for Lunar Client (Minecraft 1.8.9) via JNI injection.

## Features

- **Combat**: AutoClicker, AimAssist, Velocity, Reach, AutoRefill
- **Visual**: NameTags, ESP, Tracers, Notifications, ArrayList
- **Utility**: NoJumpDelay
- **UI**: Custom ImGui overlay with modular settings

## Quick start

```powershell
# Clone with the Themida submodule
git clone --recursive https://github.com/SyraTechnology/Cobble
cd Cobble

# Place your personal Themida license:
# copy TMLicenseA1.dat → themida\TMLicenseA1.dat

$env:PAYLOAD_KEY = "<64-char-hex>"   # must match the server's PAYLOAD_KEY
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
| `cobble-themida` | Themida SDK + project | git submodule of this repo at `themida/` |
| `cobble-server`  | Go 1.22 + PostgreSQL  | license API, payload key, telemetry, signed updates |
| `cobble-web`     | Next.js 16 / React 19 | public site, dashboard, payments |
| `cobble-bot`     | Go + discordgo        | Discord ticket bot |

## Layout

```
.
├── Cobble.sln
├── Cobble.vcxproj
├── build.ps1
├── CHANGELOG.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT.md
│   └── UPDATE_WORKFLOW.md
├── src/                 client DLL (modules, JNI bridge, hooks, overlay, security)
├── loader/              injector (D3D11 + ImGui, security, updater)
├── infra/ext/           vendored: imgui, minhook, jni, stb
├── tools/               build helpers (encrypt, sign, generate keys, code quality)
└── themida/             ↳ submodule → SyraTechnology/cobble-themida
                          (Themida SDK + project; place TMLicenseA1.dat here locally)
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) – system design and threading model
- [Development](docs/DEVELOPMENT.md) – module template and conventions
- [Update workflow](docs/UPDATE_WORKFLOW.md) – release pipeline, signed manifests
- [Changelog](CHANGELOG.md)

## Requirements

- Visual Studio 2022 (with MSBuild at `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`, or edit `build.ps1`)
- Windows 10/11 x64
- Themida (installed locally; license file goes in `themida/TMLicenseA1.dat`, not in git)

## Production build

```powershell
# Same PAYLOAD_KEY value must be set on the server's .env
openssl rand -hex 32

$env:PAYLOAD_KEY = "<hex>"
.\build.ps1 -PrepareRelease   # builds + signs the update manifest
```

## License

Private.
