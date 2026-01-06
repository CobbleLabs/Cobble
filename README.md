# Cobble

Ghost client for Lunar Client via JNI injection.

## Features

- **Combat**: AutoClicker, AimAssist, Velocity, Reach, AutoRefill
- **Visual**: NameTags
- **Utility**: NoJumpDelay
- **UI**: Custom ImGui overlay with modular settings

## Quick Start

```powershell
# Build everything
.\build.ps1 -Release

# Output
out/bin/Release/CobbleLoader.exe  # Injector
out/bin/Release/Cobble.dll        # Client (embedded in loader)
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) – System design and technical decisions
- [Development](docs/DEVELOPMENT.md) – How to create modules and work in the codebase
- [TODO](docs/TODO.md) – Roadmap and improvements
- [Changelog](CHANGELOG.md) – Version history

## Project Structure

```
Cobble/
├── loader/           # Injector (C++)
├── license-server/   # Backend API (Go + PostgreSQL)
├── website/          # Frontend (Next.js)
├── src/              # Client DLL
│   ├── module/       # Feature modules
│   ├── ui/           # ImGui overlay
│   ├── sdk/          # Minecraft wrappers
│   └── jni/          # JVM bridge
├── docs/             # Documentation
└── out/bin/Release/  # Build output
```

## Requirements

- Visual Studio 2022
- Windows 10/11 x64
- Lunar Client (Minecraft 1.8.9)

## Server Deployment

```bash
# License Server
cd license-server
sudo bash deploy/deploy.sh

# Website (on VPS)
cd website
npm install
npm run build
pm2 start npm --name "cobble-web" -- start
```

## License

Private
