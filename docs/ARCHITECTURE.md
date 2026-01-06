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

### Packet Interceptor (JVMTI Bytecode Path)

The PacketInterceptor hooks Java bytecode at class-load time to intercept outbound packets without JVMTI method entry events.

- JVMTI ClassFileLoadHook patches `NetworkManager.sendPacket(...)` to call a native bridge before the original body.
- The native bridge (PacketHook) can queue or pass through movement packets.
- Cached `jclass` references are stored as GlobalRefs to stay valid across GC.
- `Java::ClassLoader` is cached as a GlobalRef to resolve classes consistently.

### Blink Flow (PacketInterceptor)

Blink now relies on PacketInterceptor rather than `lastReportedPos` mutation.

1. `Blink::onEnable()` -> `PacketInterceptor::setBlinkActive(true)`
2. Movement packets are queued while Blink is active.
3. `Blink::onDisable()` -> `PacketInterceptor::setBlinkActive(false)`
4. A drain flush sends queued packets in small batches during `PacketInterceptor::Tick()`.

### Overlay Context Stability

Overlay reinit is guarded to avoid rapid context churn.

- Reinit only triggers when the OpenGL context changes.
- The overlay binds to the first valid window/context and ignores other windows.
- A cooldown/suspend window prevents continuous reinit loops.
- `wglMakeCurrent` failure skips rendering for that frame.

### License Server (`license-server/`)

| Component | Responsibility |
|-----------|----------------|
| `handlers/auth.go` | JWT auth (register, login, refresh, logout, me) |
| `handlers/verify.go` | HWID verification with signed responses |
| `handlers/loader.go` | Loader login (username+password → HWID bind) |
| `handlers/profile.go` | User profile, password change, HWID reset |
| `handlers/dashboard.go` | Announcements, changelogs, downloads |
| `handlers/admin.go` | User/license CRUD, announcements, logs |
| `middleware/` | CORS, JWT auth, API key auth, rate limiting |
| `database/` | PostgreSQL persistence |

#### Loader Authentication Flow (v2)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    FIRST RUN (No stored HWID)                            │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Loader prompts: "Username:" + "Password:"                           │
│  2. POST /api/loader/login { username, password, hwid }                 │
│  3. Server:                                                             │
│     ├── Verify credentials                                              │
│     ├── Check if user has license                                       │
│     ├── Check HWID slots (max 2 devices)                                │
│     ├── Bind HWID to license                                            │
│     └── Return signed response { valid, nonce, expires, plan }          │
│  4. Loader stores HWID → %APPDATA%/Cobble/hwid.dat                     │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│                    SUBSEQUENT RUNS (HWID exists)                         │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Loader reads stored HWID                                            │
│  2. POST /api/verify { hwid, version, nonce }                           │
│  3. Server signs response with Ed25519 private key                      │
│  4. Loader verifies signature with hardcoded public key                 │
│  5. Check nonce matches (anti-replay)                                   │
│  6. Check expiry timestamp                                              │
│  7. If valid → inject DLL                                               │
└─────────────────────────────────────────────────────────────────────────┘
```

> **C++ Developer Note**: Update loader to use `/api/loader/login` for first run.
> Prompt username+password when no hwid.dat exists. Store HWID on success.

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
