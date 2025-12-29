# Cobble - Architecture

Ghost client inyectable para Lunar Client (Minecraft) via JNI.

## Estructura del Proyecto

```
Cobble/
├── src/
│   ├── main.cpp              # Entry point (DllMain)
│   ├── cobble.cpp/.hpp       # Context principal y ciclo de vida
│   ├── jni/
│   │   └── java.cpp/.h       # Puente JNI y ClassLoader
│   ├── sdk/
│   │   ├── version.h         # Enum de versiones MC
│   │   ├── jstring_util.h    # Conversión jstring ↔ std::string
│   │   ├── minecraft.cpp/.h  # Wrapper Minecraft (thePlayer, theWorld, etc)
│   │   └── player.cpp/.h     # Wrapper LocalPlayer (pos, motion, health, etc)
│   ├── module/
│   │   ├── module.h          # Base class para módulos
│   │   ├── module_manager.h/.cpp  # Registro, keybinds, sync bidireccional
│   │   ├── autoclicker.h/.cpp     # AutoClicker con randomización
│   │   ├── aimassist.h/.cpp       # AimAssist con física spring-damper
│   │   └── nametags.h/.cpp        # ESP de nombres de jugadores
│   ├── overlay/
│   │   ├── overlay.h/.cpp         # Sistema de overlay con ImGui/OpenGL
│   │   └── notifications.h/.cpp   # Notificaciones in-game (módulos + sistema)
│   ├── web/
│   │   ├── config_server.h/.cpp   # WebSocket server (WinSock)
│   │   └── config.h/.cpp          # Parser JSON y sync de config
│   └── util/
│       └── logger.hpp        # Sistema de logging
├── injector/
│   ├── main.cpp              # CLI del inyector
│   ├── inject.cpp/.h         # Lógica de inyección DLL
│   └── process.cpp/.h        # Búsqueda de proceso Minecraft
├── config/
│   └── index.html            # Web GUI para configuración
├── ext/
│   ├── jni/                  # Headers JNI y jvm.lib
│   ├── imgui/                # ImGui para overlay UI
│   └── minhook/              # Hook library para OpenGL
└── bin/                      # Binarios compilados
```

## Ciclo de Vida

```
CobbleInjector.exe
    ├── Detecta Minecraft/Lunar Client
    ├── Verifica si ya está inyectado (isDllInjected)
    └── Inyecta Cobble.dll via CreateRemoteThread

Cobble.dll (DllMain)
    └── CreateThread → cobble::context::initialize()
                           ├── Java::Init()               # JNI + ClassLoader
                           ├── SDK_Init()                 # Minecraft wrapper
                           ├── moduleManager.register()   # Registrar módulos
                           ├── Web_Init()                 # WebSocket server
                           ├── Main loop (1ms tick)
                           │     ├── Overlay::Init() (delayed 1s)
                           │     ├── NotificationManager::addSystem("Cobble loaded")
                           │     ├── player.refresh()
                           │     ├── moduleManager.update()
                           │     │     ├── isMinecraftFocused() check
                           │     │     ├── isInGui() check (bloquea keybinds en menús)
                           │     │     └── Web_BroadcastConfig() on toggle
                           │     └── module.onUpdate()
                           └── shutdown() → Web_Shutdown() → Java::Kill() → FreeLibrary
```

## Sistema de Inyección

### Injector (`injector/`)
- **Detección de duplicados**: `isDllInjected()` previene inyección múltiple
- **Mensaje si ya inyectado**: "Cobble is already injected! Use the web UI to eject first."
- **Inyección**: LoadLibraryW via CreateRemoteThread
- **Eject**: FreeLibrary via CreateRemoteThread (desde web UI)

## Componentes Principales

### SDK (`src/sdk/`)
- **Minecraft**: Wrapper para `net.minecraft.client.Minecraft`
  - `getPlayer()`, `getWorld()`, `isInGui()`, `isTargetingBlock()`
  - `clickMouse()`, `setLeftClickCounter()`, `setRightClickDelay()`
  - `getPlayerList()`, `canEntityBeSeen()`
- **LocalPlayer**: Wrapper para EntityPlayerSP + EntityLivingBase + Entity
  - Posición, movimiento, rotación (`getYaw()`, `setYaw()`, etc)
  - `getHealth()`, `getHurtTime()`, `isOnGround()`
  - `isSprinting()`, `setSprinting()`, `jump()`
  - `getHeldItemId()` - para detección de armas

### Sistema de Módulos (`src/module/`)
- **Module**: Base class con `onEnable()`, `onDisable()`, `onUpdate()`, keybind
- **ModuleManager**: 
  - Registro de módulos
  - Edge detection en keybinds (no repeat)
  - **Bloqueo en GUI**: `isInGui()` previene toggles en chat/inventario/menús
  - **Sync bidireccional**: `Web_BroadcastConfig()` notifica a la web en cada toggle
- **AutoClicker**: 
  - SendMessage clicking (registra en CPS counters)
  - Randomización: drop chance, spike chance, kurtosis
  - Weapon-only mode (swords, axes por ID)
  - Ignore blocks (via MouseOver)
- **AimAssist**: Sistema de asistencia de apuntado con física realista
  - **Soft deadzone**: Transición suave cerca del hitbox (no on/off binario)
  - **Spring-damper**: Física con dos regímenes (corrección lejos, asentamiento cerca)
  - **Velocity-based**: Delta-time independiente para cualquier framerate
  - **Intent detection**: Detecta mouse rápido y reduce asistencia
  - **Opposition detection**: Si el usuario mueve en dirección contraria, para
  - **Temporal noise**: Ruido periódico (no por frame) para humanización
  - **Accumulated bias**: Micro-error persistente que simula mano humana
- **NameTags**: ESP de nombres de jugadores con overlay

### Overlay (`src/overlay/`)
- **Overlay**: Hooks OpenGL (wglSwapBuffers) para renderizado sobre Minecraft
  - ImGui para UI
  - Init delay: 1 segundo después de inyección
- **Notifications**: Sistema de notificaciones in-game
  - **Módulos**: "ModuleName ON/OFF" - verde/rojo
  - **Sistema**: "Cobble loaded", "Cobble unloading..." - púrpura
  - Animación slide-in/out con easing cubic
  - Posicionadas en esquina inferior derecha
  - Progress bar que decrece
  - Font: Segoe UI del sistema

### Web Config (`src/web/`)
- **ConfigServer**: WebSocket server puro WinSock (puerto 8765)
  - Handshake WebSocket con SHA-1
  - Frame encoding/decoding
  - Single client connection
- **Config**: Parser JSON simple
  - `apply()`: Recibe config del browser
  - `serialize()`: Envía config actual al browser (type: "configAll")
  - `Web_BroadcastConfig()`: Push inmediato cuando hay cambios por keybind

## Sync Bidireccional

```
┌─────────────┐                    ┌─────────────┐
│   Web UI    │◄──── configAll ────│   Client    │
│ (browser)   │                    │   (DLL)     │
│             │──── config ───────►│             │
└─────────────┘                    └─────────────┘
       │                                  │
       │  Polling cada 500ms              │
       │  (getConfig → configAll)         │
       │                                  │
       │  Push inmediato on:              │
       │  - Keybind toggle                │
       └──────────────────────────────────┘
```

## Threading

| Contexto | Thread |
|----------|--------|
| DllMain | Thread del loader |
| Main loop | Thread creado (attached a JVM) |
| WebSocket accept | Thread separado (serverLoop) |
| Overlay render | Thread de OpenGL (hooks) |
| Llamadas JNI | Solo desde main loop thread |

> **Importante**: `Java::Env` solo es válido en el thread attached. El WebSocket server no hace llamadas JNI directas.

## Build

### Visual Studio
```
Abrir Cobble.sln → Build → Debug/Release x64
Output: 
  - bin/Debug/Cobble.dll
  - bin/Debug/CobbleInjector.exe
```

## Uso

1. Abrir Minecraft via Lunar Client
2. Ejecutar `CobbleInjector.exe`
3. Abrir `config/index.html` en navegador
4. Configurar módulos via web UI
5. Usar keybinds in-game (V=AimAssist, R=AutoClicker, N=NameTags)
6. Click "Eject" en web UI para descargar

## Settings de AimAssist (simplificados)

| Setting | Rango | Descripción |
|---------|-------|-------------|
| Horizontal Speed | 1-100 | Velocidad yaw (×1.5 internamente) |
| Vertical Speed | 1-100 | Velocidad pitch (×1.5 internamente) |
| Max FOV | 30-360 | Campo de visión para detectar targets |
| Max Distance | 1-8 | Distancia máxima al target |
| Only Clicking | on/off | Solo cuando click izquierdo |
| Only Sprinting | on/off | Solo cuando corriendo |
| Vertical Aim | on/off | Ajustar pitch también |
| Visibility Check | on/off | Solo targets visibles |
| Ignore Invisible | on/off | Ignorar jugadores invisibles |
| Target Lock | on/off | Mantener target actual |

## Notas de Estabilidad

- Sin consola: toda interacción via Web UI (evita crashes al cerrar)
- DeleteLocalRef para evitar memory leaks de JNI
- WIN32_LEAN_AND_MEAN antes de WinSock2.h
- Loop principal: 1ms delay para responsividad
- Edge detection en keybinds (no repeat)
- Keybinds bloqueados cuando isInGui() = true
- WebSocket reconecta automáticamente
- AimAssist usa deltaTime para framerate-independence
- Overlay init delay: 1 segundo (permite que OpenGL se estabilice)

## AimAssist - Física Detallada

### Sistema de Control
```
Error (ángulo al target)
    ↓
Soft Strength (S-curve basada en distancia al hitbox)
    - Lejos del hitbox: strength = 1.0
    - En el borde: smooth fade
    - Centro: strength = 0.05
    ↓
Spring-Damper con dos regímenes:
    - Lejos (>3°): Spring activo (corrección)
    - Cerca (<3°): Spring fade, solo damping (asentamiento)
    ↓
Opposition + Intent factors (reduce si usuario resiste)
    ↓
Velocity integration (deltaTime)
    ↓
Rotación final
```

### Funciones Clave
- `angleToCenterCircular()`: Ángulo al centro del hitbox (wrap-safe)
- `calculateSoftStrength()`: S-curve 0.05-1.0 basada en posición
- `calculateSmoothRotation()`: Spring-damper con stiffness fade
