# Cobble - Architecture

Ghost client inyectable para Lunar Client (Minecraft) via JNI.

## Estructura del Proyecto

```
Cobble/
├── loader/                       # Inyector/Loader (producción)
│   ├── src/
│   │   ├── core/                 # Inyección y detección de procesos
│   │   │   ├── inject.cpp/.h     # Manual Mapping injection (Secure RX/RW)
│   │   │   └── process.cpp/.h    # Detección de Minecraft
│   │   ├── security/             # Anti-cracking
│   │   │   ├── security.cpp/.h   # Anti-debug, anti-VM, anti-hook
│   │   │   └── xor.h             # Compiletime string encryption
│   │   └── license/              # Sistema de licencias
│   │       ├── hwid.cpp/.h       # Generación HWID
│   │       └── license.cpp/.h    # Verificación HTTP (Primary + Failover)
│   ├── res/                      # Recursos
│   │   ├── resource.h            # IDs de recursos
│   │   └── CobbleLoader.rc       # DLL embebido (Única fuente de inyección)
│   ├── main.cpp                  # Entry point
│   └── CobbleLoader.vcxproj
│
├── license-server/               # Backend de licencias (Go)
│   ├── main.go                   # Entry point (Rate Limited)
│   ├── .env.example              # Template de configuración
│   ├── go.mod                    # Dependencias
│   ├── internal/
│   │   ├── config/               # Variables de entorno + godotenv
│   │   │   └── config.go
│   │   ├── logger/               # Logging estructurado
│   │   │   └── logger.go
│   │   ├── database/             # SQLite + Postgres support
│   │   │   └── db.go
│   │   ├── models/               # Structs
│   │   │   └── license.go
│   │   ├── handlers/             # Endpoints HTTP
│   │   │   ├── verify.go         # POST /api/verify
│   │   │   ├── register.go       # POST /api/register
│   │   │   └── admin.go          # /api/admin/*
│   │   ├── middleware/           # Auth & Security
│   │   │   ├── auth.go           # Admin key middleware
│   │   │   └── ratelimit.go      # IP-based rate limiting
│   │   ├── helpers/              # Utilidades
│   │   │   └── helpers.go        # IP extraction (IPv6/XFF support)
│   │   └── discord/              # Bot de Discord
│   │       └── bot.go
│   └── deploy/                   # Scripts de deploy
│       ├── deploy.sh             # Deploy automático Ubuntu
│       ├── cobble.service        # Systemd service
│       └── Caddyfile             # HTTPS reverse proxy
│
├── src/                          # Client DLL (ghost client)
│   ├── main.cpp                  # DllMain
│   ├── cobble.cpp/.hpp           # Context y ciclo de vida (DLL_PROCESS_DETACH safe)
│   ├── jni/                      # Puente JNI (Leak-free wrapper)
│   ├── sdk/                      # Wrappers Minecraft
│   ├── module/                   # Módulos (combat, visual)
│   ├── overlay/                  # ImGui + OpenGL hooks
│   ├── module/                   # Módulos (combat, visual)
│   └── overlay/                  # ImGui + OpenGL hooks
│
└── bin/                          # Output
    └── Release/
        ├── CobbleLoader.exe      # Inyector (Stand-alone)
        └── Cobble.dll            # DLL (embebido, NO distribuido suelto)
```

---

## Arquitectura de Alto Nivel

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           USUARIO                                       │
└─────────────────────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐            ┌─────────────┐
│                 │            │   (Java)    │
│ ┌─────────────┐ │            │ ┌─────────┐ │
│ │ Security    │ │            │ │Cobble.dll│
│ │ - Anti-Debug│ │            │ │         │ │
│ │ - Anti-VM   │ │            │ │ ┌─────┐ │ │
│ │ - Anti-Dump │ │            │ │ │ JNI │ │ │
│ └─────────────┘ │            │ │ └─────┘ │ │
│        │        │            │ │ ImGui   │ │
│        ▼        │            │ │ Overlay │ │
│ ┌─────────────┐ │◄──HTTPS───►│ └─────────┘ │
│ │ License     │ │            └─────────────┘
│ │ Check       │ │
│ └─────────────┘ │
│        │        │
│        ▼        │                    ┌─────────────────────┐
│ ┌─────────────┐ │                    │  License Server     │
│ │ Manual Map  │ │      HTTPS         │  (Go + SQLite/PG)   │
│ │ Injection   │─┼────────────────────┤                     │
│ └─────────────┘ │                    │  - /api/verify      │
│                 │                    │  - /api/admin/*     │
│                 │                    │  - Rate Limiting    │
│                 │                    │  - Anti-Abuse       │
│                 │                    └─────────────────────┘
```

---

> [!NOTE]
> **License Failover**:
> El loader intenta conectar primero al servidor primario (`syra.club`). Si falla tras 4s, realiza un fallback automático a la IP de respaldo (`172.235.136.152`). Esto garantiza disponibilidad incluso si hay problemas de DNS o dominio.

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
    │     ├── License::Verify(Primary) → POST http://syra.club/api/verify (4s timeout)
    │     │     ├── Success → continue
    │     │     └── Fail (Timeout/Error) → Show "Retrying..."
    │     │           └── License::Verify(Fallback) → POST http://172.235.136.152/api/verify
    │     │                 ├── Success → continue
    │     │                 └── Fail → exit
    │     └── Server tracks HWID history (IP + Rate Limit)
    │
    ├─[3] DLL Loading
    │     └── **Secure Mode**: Only load from embedded resource (RCDATA)
    │
    ├─[4] Process Detection
    │     ├── findLunarClient() → busca javaw.exe con "Lunar"
    │     └── isDllInjected() → verifica signature en memoria
    │
    └─[5] Manual Map Injection
          ├── Allocate memory in target
          ├── Copy PE headers + sections
          ├── **Secure Protections**: Apply correct RX/RW permissions (No RWX)
          ├── Process relocations
          ├── Resolve imports (LoadLibrary, GetProcAddress)
          ├── Execute TLS callbacks
          └── Call DllMain(DLL_PROCESS_ATTACH)

Cobble.dll (DllMain)
    └── CreateThread → cobble::context::initialize()
                           ├── Java::Init()               # JNI attach
                           ├── SDK_Init()                 # Minecraft wrappers
                           ├── moduleManager.register()   # Combat, Visual
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
| **Manual Map** | **RX/RW Pages**: Protecciones correctas por sección (elimina RWX persistente). |
| **Secure Load** | **No Disk IO**: Inyección directa desde recurso embebido. |
| **Anti-Debug** | PEB.BeingDebugged, CheckRemoteDebuggerPresent, NtQueryInformationProcess, Hardware breakpoints (`RtlCaptureContext`) |
| **Anti-Attach** | DbgBreakPoint patch, DbgUiRemoteBreakin patch |
| **Anti-VM** | Registry keys, VM files, ACPI tables, Process names |
| **Networking** | URL Encoding, HTTP Status Validation, Failover, Timeouts (4s) |

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
- **Rate Limit**: Max 60 requests/minute per IP (Anti-DDoS layer)
- **IP Protection**: X-Forwarded-For trust + IPv6 support


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
| Overlay | OpenGL thread | ImGui rendering |
