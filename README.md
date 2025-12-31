# Cobble

Ghost client for Lunar Client (Minecraft) via JNI injection.

## Features

- **Combat**: AutoClicker, AimAssist, Velocity, Reach, AutoRefill
- **Visual**: NameTags ESP
- **Utility**: NoJumpDelay (bhop)
- **UI**: Custom ImGui overlay with modular settings

## Quick Start

```powershell
# Build everything
.\build.ps1 -Release

# Output
bin/Release/CobbleLoader.exe  # Injector
bin/Release/Cobble.dll        # Client (embedded in loader)
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) – System design and technical decisions
- [Development](docs/DEVELOPMENT.md) – How to create modules and work in the codebase
- [Changelog](CHANGELOG.md) – Version history

## Project Structure

```
Cobble/
├── loader/           # Injector (C++)
├── license-server/   # Backend (Go)
├── src/              # Client DLL
│   ├── module/       # Feature modules
│   ├── ui/           # ImGui overlay
│   ├── sdk/          # Minecraft wrappers
│   └── jni/          # JVM bridge
├── docs/             # Documentation
└── bin/Release/      # Build output
```

## Requirements

- Visual Studio 2022
- Windows 10/11 x64
- Lunar Client (Minecraft 1.8.9)

## License

Private