# Cobble – Architecture Overview

Cobble is an injectable ghost client for Lunar Client (Minecraft), composed of three independent but coordinated systems:

- **Loader** (C++) – Injection, security, licensing
- **Client DLL** (C++ / JNI) – Modules, SDK, UI
- **License Server** (Go) – HWID tracking, rate limiting

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              USER                                        │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐            ┌─────────────┐
│  CobbleLoader   │            │  Minecraft  │
│ ┌─────────────┐ │            │ ┌─────────┐ │
│ │ Security    │ │            │ │Cobble.dll│ │
│ │ - Anti-Debug│ │            │ │         │ │
│ │ - Anti-VM   │ │            │ │ ┌─────┐ │ │
│ │ - Anti-Dump │ │            │ │ │ JNI │ │ │
│ └─────────────┘ │            │ │ └─────┘ │ │
│        │        │            │ │ ImGui   │ │
│        ▼        │            │ │ Overlay │ │
│ ┌─────────────┐ │◄──HTTPS───►│ └─────────┘ │
│ │ License     │ │            └─────────────┘
│ │ Check       │ │
│ └─────────────┘ │                    ┌─────────────────────┐
│        │        │                    │  License Server     │
│        ▼        │      HTTPS         │  (Go + SQLite/PG)   │
│ ┌─────────────┐ │────────────────────┤                     │
│ │ LoadLibrary │ │                    │  - /api/verify      │
│ │ Injection   │ │                    │  - /api/admin/*     │
│ └─────────────┘ │                    │  - Rate Limiting    │
└─────────────────┘                    └─────────────────────┘
```

---

## Component Responsibilities

### Loader (`loader/`)

| Component | Responsibility |
|-----------|----------------|
| `inject.cpp` | LoadLibrary injection into javaw.exe |
| `process.cpp` | Minecraft/Lunar process detection |
| `security.cpp` | Anti-debug, anti-VM, anti-attach |
| `license.cpp` | HTTPS license verification with failover |
| `hwid.cpp` | SHA256(CPU + Disk + MAC) generation |

### Client DLL (`src/`)

| Component | Responsibility |
|-----------|----------------|
| `jni/` | JVM attach, JNIEnv management, class discovery |
| `sdk/` | Minecraft wrappers (player, world, inventory) |
| `module/` | Feature modules (combat, visual, utility) |
| `ui/` | ImGui overlay, menu, widgets, HUD |

### License Server (`license-server/`)

| Component | Responsibility |
|-----------|----------------|
| `handlers/` | API endpoints (verify, admin, config) |
| `middleware/` | Rate limiting, auth |
| `database/` | SQLite/PostgreSQL persistence |

### Config System (`src/config/`)

| Component | Responsibility |
|-----------|----------------|
| `config.hpp` | ConfigManager singleton, lifecycle |
| `config.cpp` | Module serialization/deserialization |
| `util/json.hpp` | JSON parser and serializer |
| `util/http.hpp` | WinHTTP client for server sync |
| `util/hwid.hpp` | Hardware ID generation |

---

## Config Persistence Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         STARTUP FLOW                                 │
├─────────────────────────────────────────────────────────────────────┤
│  1. Load %APPDATA%/Cobble/config.json (instant)                     │
│  2. Apply to modules immediately                                     │
│  3. Async fetch from server (non-blocking)                          │
│  4. Merge server config → apply → save local cache                  │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        SETTINGS CHANGE FLOW                          │
├─────────────────────────────────────────────────────────────────────┤
│  1. User changes setting in menu                                     │
│  2. Menu close → saveLocal() (immediate)                            │
│  3. Debounce 400ms → pushServerConfig() (async)                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Config JSON Structure

```json
{
  "version": 1,
  "flags": {
    "experimentalUnlocked": false
  },
  "hud": {
    "arrayList": true,
    "notifications": true
  },
  "modules": {
    "AutoClicker": {
      "enabled": false,
      "keybind": 0,
      "minCps": 10.0,
      "maxCps": 12.0
    },
    "AimAssist": {
      "enabled": false,
      "keybind": 0,
      "speed": 50.0,
      "fov": 180
    }
  }
}
```

### Server Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/config` | GET | Fetch stored config by HWID |
| `/api/config/save` | POST | Save config (HWID + JSON) |

---

## Execution Flow

```
CobbleLoader.exe
    │
    ├─[0] UPDATE MODE CHECK
    │     └── If --update-pid args present → Act as updater
    │           ├── Wait for old process to exit
    │           ├── Backup old.exe → old.exe.old
    │           ├── Move new.exe → old.exe location
    │           ├── Launch updated loader (CREATE_NEW_CONSOLE)
    │           └── Exit (return 0)
    │
    ├─[1] Security Initialization
    │     ├── IsOnVM() → exit if VM detected
    │     ├── IsBeingDebugged() → exit if debugger
    │     └── Start heartbeat threads (monitoring)
    │
    ├─[2] UI Init + UPDATE CHECK
    │     ├── setupConsole() + printBanner()
    │     ├── Updater::Check() → fetch manifest from server
    │     │     ├── Verify Ed25519 signature
    │     │     └── Compare current vs remote version
    │     ├── If update available (optional):
    │     │     └── Prompt "Press Y to update..."
    │     ├── If update required (blocked/mandatory):
    │     │     └── Auto-download + apply
    │     ├── Download to %TEMP%\cb_xxx\
    │     ├── Verify SHA256 + size
    │     ├── Spawn new loader with --update-pid args
    │     └── Exit (new loader takes over)
    │
    ├─[3] License Verification
    │     ├── HWID::Generate()
    │     ├── License::Verify(Primary) → 4s timeout
    │     └── Fallback to secondary server if needed
    │
    ├─[4] Process Detection
    │     └── findLunarClient() → javaw.exe with "Lunar"
    │
    └─[5] LoadLibrary Injection
          ├── Write DLL to temp file
          ├── CreateRemoteThread(LoadLibraryW)
          └── Delete temp file

Cobble.dll (DllMain)
    └── CreateThread → cobble::context::initialize()
                           │
                           ├── c_logger::initialize()
                           ├── Java::Init()
                           ├── SDK_Init()
                           │
                           ├── Dynamic Module Allocation
                           │     ├── moduleManager = new ModuleManager()
                           │     ├── autoClicker = new AutoClicker()
                           │     └── ... (all modules)
                           │
                           ├── moduleManager->registerModule(*)
                           │
                           ├── ConfigManager::get().init()
                           │     ├── Load local config.json
                           │     ├── Apply to modules
                           │     └── Async fetch server config
                           │
                           ├── Overlay::Init()
                           │
                           └── Main loop (1ms tick)
                                 ├── Integrity::Verify() (periodic)
                                 ├── player.refresh()
                                 └── moduleManager->update()

cobble::context::shutdown()
    ├── ConfigManager::get().shutdown()  // Save config
    ├── Disable all modules
    ├── Overlay::Shutdown()
    ├── Java::Kill()
    └── delete all module pointers
```

---

## Module System Architecture

### Ownership Model

```
cobble::context owns:
├── moduleManager (unique ownership)
├── autoClicker (unique ownership)
├── aimAssist (unique ownership)
└── ... all modules

ModuleManager references:
└── modules[] (non-owning pointers)
```

**Rules:**
- All modules are heap-allocated at runtime (no static globals)
- `cobble::context` is the sole owner and deleter
- `ModuleManager` only holds references for iteration
- Modules are self-contained and own their settings

### Module Lifecycle

```
new Module() → registerModule() → onEnable/onDisable → onUpdate (tick) → delete
```

### Base Class

```cpp
struct Module {
    const char* name;
    bool enabled = false;
    int keybind = 0;

    virtual void onEnable() {}
    virtual void onDisable() {}
    virtual void onUpdate() {}
    virtual void onRender() {}
    virtual void onRenderMenu() {}  // Self-rendering UI
};
```

---

## UI Architecture

### Design Philosophy

The UI follows a **decentralized rendering model**:

- Each module renders its own settings via `onRenderMenu()`
- No central "Settings" page knows about individual modules
- Menu framework only orchestrates navigation
- Widgets are consolidated and reusable

### Directory Structure

```
src/ui/
├── core/overlay.cpp       # OpenGL hooks, ImGui init
├── widgets/
│   ├── widgets.hpp/.cpp   # Consolidated widgets
│   ├── theme.hpp/.cpp     # Color scheme
│   └── icons.hpp          # FontAwesome definitions
├── menu/menu.cpp          # Tab navigation, dynamic routing
└── hud/
    ├── arraylist.cpp      # Active modules display
    └── notifications.cpp  # Toast system
```

### Rendering Flow

```
Overlay::Render()
    └── Menu::Render()
            ├── Render tabs (left nav)
            ├── Get current module via getModuleForSubtab()
            └── module->onRenderMenu()  // Module renders itself
```

### UI Hard Rules

1. **Never call module-specific code from menu.cpp** – only `onRenderMenu()`
2. **Use `ui::two_column()` for standard layouts**
3. **All widgets live in `widgets.cpp`** – no duplicates
4. **Scaling is automatic** – use `ui::s(value)` for sizes

---

## Threading Model

| Context | Thread | Safety Notes |
|---------|--------|--------------|
| Loader main | Main thread | Security init, injection |
| Heartbeat 1 | Background | Anti-debug monitoring |
| Heartbeat 2 | Background | Anti-hook monitoring |
| DLL main loop | JNI thread | Attached to JVM |
| Overlay render | OpenGL thread | ImGui calls only here |

**Critical:** Never call JNI from the OpenGL thread. Never call ImGui from the JNI thread.

---

## Security Model

### Loader Security

| Layer | Implementation |
|-------|----------------|
| Anti-Debug | PEB.BeingDebugged, NtQueryInformationProcess, hardware breakpoints |
| Anti-Attach | DbgBreakPoint patch, DbgUiRemoteBreakin patch |
| Anti-VM | Registry keys, VM files, ACPI tables, process names |
| Injection | LoadLibrary (no persistent RWX memory) |
| Integrity | SHA256 hash verification before inject |

### HWID Generation

```
HWID = SHA256(CPU_ID + DISK_SERIAL + MAC_ADDRESS)
```

### License Server Protection

- Rate limiting (60 req/min per IP)
- HWID abuse detection (3+ unique HWIDs = ban)
- X-Forwarded-For trust with validation
- Admin endpoints require API key

---

## Design Principles

| Principle | Implementation |
|-----------|----------------|
| **Separation of concerns** | JNI / SDK / Modules / UI are isolated |
| **No static initialization** | All modules heap-allocated at runtime |
| **Fail-fast security** | Exit immediately on any detection |
| **UI is declarative** | Modules describe their UI, framework renders |
| **Modules are self-contained** | Own their settings, keybinds, and UI |
| **Single source of truth** | One widget implementation, one theme, one render path |

---

## Build Commands

```powershell
# Build everything (DLL + Loader with hash verification)
.\build.ps1 -Release

# Build only DLL
msbuild Cobble.vcxproj /p:Configuration=Release /p:Platform=x64

# Build only Loader
msbuild CobbleLoader.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Deploy Server

```bash
cd license-server
sudo bash deploy/deploy.sh
```
