# Cobble - Architecture

Ghost client inyectable para Lunar Client (Minecraft) via JNI.

## Estructura del Proyecto

```
Cobble/
├── loader/                       # Inyector/Loader (producción)
│   ├── src/
│   │   ├── core/                 # Inyección y detección de procesos
│   │   │   ├── inject.cpp/.h     # Manual Mapping injection
│   │   │   └── process.cpp/.h    # Detección de Minecraft
│   │   ├── security/             # Anti-cracking
│   │   │   ├── security.cpp/.h   # Anti-debug, anti-VM, anti-hook
│   │   │   └── xor.h             # Compiletime string encryption
│   │   └── license/              # Sistema de licencias
│   │       ├── hwid.cpp/.h       # Generación HWID (CPU+Disk+MAC)
│   │       └── license.cpp/.h    # Verificación HTTP con servidor
│   ├── res/                      # Recursos
│   │   ├── resource.h            # IDs de recursos
│   │   └── CobbleLoader.rc       # DLL embebido
│   ├── main.cpp                  # Entry point
│   └── CobbleLoader.vcxproj
│
├── license-server/               # Backend de licencias (Go)
│   ├── cmd/
│   │   └── main.go               # Server HTTP + SQLite
│   ├── internal/                 # (Módulos futuros)
│   ├── deploy/                   # Scripts de deploy
│   │   ├── deploy.sh             # Deploy automático Ubuntu
│   │   ├── cobble.service        # Systemd service
│   │   └── Caddyfile             # HTTPS reverse proxy
│   └── migrations/               # SQL schemas
│
├── src/                          # Client DLL (ghost client)
│   ├── main.cpp                  # DllMain
│   ├── cobble.cpp/.hpp           # Context y ciclo de vida
│   ├── jni/                      # Puente JNI
│   ├── sdk/                      # Wrappers Minecraft
│   ├── module/                   # Módulos (combat, visual)
│   ├── overlay/                  # ImGui + OpenGL hooks
│   └── web/                      # WebSocket config server
│
├── config/                       # Web UI
│   └── index.html                # Configuración en browser
│
└── bin/                          # Output
    └── Release/
        ├── CobbleLoader.exe      # Inyector
        └── Cobble.dll            # DLL (embebido en .exe)
```

---

## Arquitectura de Alto Nivel

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USUARIO                                       │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │                        │
         ▼                              ▼                        ▼
┌─────────────────┐            ┌─────────────┐          ┌───────────────┐
│ CobbleLoader.exe│            │  Minecraft  │          │   Web UI      │
│                 │            │   (Java)    │          │  (Browser)    │
│ ┌─────────────┐ │            │             │          │               │
│ │ Security    │ │            │ ┌─────────┐ │          │ ┌───────────┐ │
│ │ - Anti-Debug│ │            │ │Cobble.dll│◄──────────┤ │ WebSocket │ │
│ │ - Anti-VM   │ │            │ │         │ │  :8765   │ │  Client   │ │
│ │ - Anti-Hook │ │            │ │ ┌─────┐ │ │          │ └───────────┘ │
│ └─────────────┘ │            │ │ │ JNI │ │ │          └───────────────┘
│        │        │            │ │ └─────┘ │ │
│        ▼        │            │ │ ImGui   │ │
│ ┌─────────────┐ │◄──HTTPS───►│ │ Overlay │ │
│ │ License     │ │            │ └─────────┘ │
│ │ Check       │ │            └─────────────┘
│ └─────────────┘ │
│        │        │
│        ▼        │                    ┌─────────────────────┐
│ ┌─────────────┐ │                    │  License Server     │
│ │ Manual Map  │ │      HTTPS         │  (Go + SQLite)      │
│ │ Injection   │─┼────────────────────┤                     │
│ └─────────────┘ │                    │  - /api/verify      │
└─────────────────┘                    │  - /api/admin/*     │
                                       │  - HWID tracking    │
                                       │  - Auto-ban         │
                                       └─────────────────────┘
```

---

## Flujo de Ejecución

```
CobbleLoader.exe
    │
    ├─[1] Security Initialization
    │     ├── IsOnVM() → exit si detecta VM/sandbox
    │     ├── IsBeingDebugged() → exit si debugger
    │     ├── Initialize() → patches anti-attach
    │     └── Start heartbeat threads (monitoring)
    │
    ├─[2] License Verification
    │     ├── HWID::Generate() → SHA256(CPU + Disk + MAC)
    │     ├── License::Verify() → POST /api/verify
    │     │     ├── Success → continue
    │     │     └── Fail → show HWID, copy to clipboard, exit
    │     └── Server tracks HWID history (auto-ban on abuse)
    │
    ├─[3] DLL Loading
    │     ├── Load from embedded resource (RCDATA)
    │     └── Fallback to bin/Release/Cobble.dll
    │
    ├─[4] Process Detection
    │     ├── findLunarClient() → busca javaw.exe con "Lunar"
    │     └── isDllInjected() → verifica si ya inyectado
    │
    └─[5] Manual Map Injection
          ├── Allocate memory in target
          ├── Copy PE headers + sections
          ├── Process relocations
          ├── Resolve imports (LoadLibrary, GetProcAddress)
          ├── Execute TLS callbacks
          └── Call DllMain(DLL_PROCESS_ATTACH)

Cobble.dll (DllMain)
    └── CreateThread → cobble::context::initialize()
                           ├── Java::Init()               # JNI attach
                           ├── SDK_Init()                 # Minecraft wrappers
                           ├── moduleManager.register()   # Combat, Visual
                           ├── Web_Init()                 # WebSocket :8765
                           ├── Overlay::Init()            # ImGui hooks
                           └── Main loop (1ms tick)
                                 ├── Security heartbeat check
                                 ├── player.refresh()
                                 ├── moduleManager.update()
                                 └── module.onUpdate()
```

---

## Sistema de Seguridad

### Loader Security (`loader/src/security/`)

| Categoría | Checks |
|-----------|--------|
| **Anti-Debug** | PEB.BeingDebugged, CheckRemoteDebuggerPresent, NtQueryInformationProcess, Heap flags, Hardware breakpoints |
| **Anti-Attach** | DbgBreakPoint patch, DbgUiRemoteBreakin patch |
| **Anti-VM** | Registry keys, VM files, ACPI tables, Process names |
| **Anti-Sandbox** | Uptime < 10min, Disk < 50GB, Suspicious usernames/hostnames, Analysis tools |
| **Anti-Hook** | API prologue checks (0xCC, 0xE9) |
| **Runtime** | Dual heartbeat threads, integrity monitoring |

### String Encryption (`xor.h`)
- Compiletime XOR encryption
- Strings decrypted at runtime on stack
- Zero runtime overhead

### HWID Generation (`hwid.cpp`)
```
HWID = SHA256(CPU_ID + DISK_SERIAL + MAC_ADDRESS)
```
- CPU: `__cpuid` instruction
- Disk: `GetVolumeInformationW`
- MAC: `GetAdaptersInfo`

---

## License Server

### Endpoints

| Endpoint | Auth | Description |
|----------|------|-------------|
| `POST /api/verify` | - | Verify HWID |
| `POST /api/admin/add` | Key | Add/update license |
| `POST /api/admin/ban` | Key | Ban user |
| `POST /api/admin/unban` | Key | Unban + clear history |
| `GET /api/admin/list` | Key | List all licenses |
| `GET /api/admin/stats` | Key | Statistics |

### HWID Abuse Detection
- Tracks all HWIDs per username
- 3+ unique HWIDs → automatic ban
- Prevents license sharing

---

## Módulos

### Combat
| Módulo | Key | Descripción |
|--------|-----|-------------|
| AutoClicker | R | Humanized clicking con fatigue/bursts |
| AimAssist | V | Spring-damper physics, deadzone, opposition detection |

### Visual
| Módulo | Key | Descripción |
|--------|-----|-------------|
| NameTags | N | ESP con nombres de jugadores |

---

## Build

```powershell
# Build todo
msbuild Cobble.sln /p:Configuration=Release

# Solo loader
msbuild Cobble.sln /p:Configuration=Release /t:CobbleLoader

# Solo DLL
msbuild Cobble.sln /p:Configuration=Release /t:Cobble
```

## Deploy Server

```bash
# Ubuntu 24.04
cd license-server
sudo bash deploy/deploy.sh
```

---

## Threading Model

| Context | Thread | Notes |
|---------|--------|-------|
| Loader main | Main thread | Security init, injection |
| Heartbeat 1 | Background | Anti-debug monitoring |
| Heartbeat 2 | Background | Anti-hook monitoring |
| DLL main loop | JNI thread | Attached to JVM |
| WebSocket | Background | Config sync |
| Overlay | OpenGL thread | ImGui rendering |
