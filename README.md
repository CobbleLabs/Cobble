# Cobble

Ghost client for Lunar Client (Minecraft 1.8.9) via JNI injection.

## Features

- **Combat**: AutoClicker, AimAssist, Velocity, Reach, AutoRefill
- **Visual**: NameTags, ESP, Tracers, Notifications, ArrayList
- **Utility**: NoJumpDelay
- **UI**: Custom ImGui overlay with modular settings

## Quick start

```powershell
$env:PAYLOAD_KEY = "<64-char-hex>"   # must match the server's PAYLOAD_KEY
.\build.ps1 -Release

# Output
out/bin/Release/CobbleLoader.exe
out/bin/Release/Cobble.dll
```

> Without `-PayloadKey` / `$env:PAYLOAD_KEY`, the build uses a legacy hardcoded key (insecure, dev only).

## Repos

This client only — backend/website/bot live in separate repos:

| Repo | Stack | Purpose |
|---|---|---|
| `cobble`            | C++20 / MSBuild       | This repo: client DLL + injector |
| `cobble-server`     | Go 1.22 + PostgreSQL  | License API, payload-key delivery, telemetry, signed updates |
| `cobble-web`        | Next.js 16 / React 19 | Public site, registration, dashboard |
| `cobble-bot`        | Go + discordgo + SQLite | Discord ticket bot |

## Layout

```
.
├── Cobble.sln
├── Cobble.vcxproj
├── build.ps1
├── CLAUDE.md            instructions for Claude Code (gitignored)
├── CHANGELOG.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT.md
│   └── UPDATE_WORKFLOW.md
├── src/                 client DLL (modules, JNI bridge, hooks, overlay, security)
├── loader/              injector (D3D11 + ImGui, security, updater)
├── infra/ext/           vendored: imgui, minhook, jni, stb
├── tools/               build helpers (encrypt, sign, generate keys, code quality)
└── themida/             Themida SDK + project (local-only; do not commit license file)
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) – system design and threading model
- [Development](docs/DEVELOPMENT.md) – module template and conventions
- [Update workflow](docs/UPDATE_WORKFLOW.md) – release pipeline, signed manifests
- [CLAUDE.md](CLAUDE.md) – constraints summary for AI tooling (gitignored)
- [Changelog](CHANGELOG.md)

## Requirements

- Visual Studio 2022 (with MSBuild at `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`, or edit `build.ps1`)
- Windows 10/11 x64
- Lunar Client (Minecraft 1.8.9) for runtime

## Production build

The DLL is encrypted with `PAYLOAD_KEY` and the server hands the decryption key to the loader at runtime.

```powershell
# 1. Generate the key once (server + client must use the same value)
openssl rand -hex 32

# 2. Set on server (cobble-server/.env): PAYLOAD_KEY=<hex>

# 3. Build the client with the same value
$env:PAYLOAD_KEY = "<hex>"
.\build.ps1 -Release
```

## License

Private.
