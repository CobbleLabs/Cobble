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
│   │   ├── module_manager.h/.cpp  # Registro y keybinds
│   │   └── autoclicker.h/.cpp     # AutoClicker con randomización
│   ├── web/
│   │   ├── config_server.h/.cpp   # WebSocket server (WinSock)
│   │   └── config.h/.cpp          # Parser JSON y sync de config
│   └── util/
│       └── logger.hpp        # Sistema de logging a consola
├── config/
│   └── index.html            # Web GUI para configuración
├── ext/
│   └── jni/                  # Headers JNI y jvm.lib
└── bin/                      # DLL compilada
```

## Ciclo de Vida

```
DllMain (DLL_PROCESS_ATTACH)
    └── CreateThread → cobble::context::initialize()
                           ├── logger.initialize()
                           ├── Java::Init()               # JNI + ClassLoader
                           ├── SDK_Init()                 # Minecraft wrapper
                           ├── moduleManager.register()   # Registrar módulos
                           ├── Web_Init()                 # WebSocket server
                           ├── Main loop (1ms tick)
                           │     ├── player.refresh()
                           │     └── moduleManager.update()
                           └── shutdown() → Web_Shutdown() → Java::Kill()
```

## Componentes Principales

### SDK (`src/sdk/`)
- **Minecraft**: Wrapper para `net.minecraft.client.Minecraft`
  - `getPlayer()`, `getWorld()`, `isInGui()`, `isTargetingBlock()`
  - `clickMouse()`, `setLeftClickCounter()`, `setRightClickDelay()`
- **LocalPlayer**: Wrapper para EntityPlayerSP + EntityLivingBase + Entity
  - Posición, movimiento, rotación
  - `getHealth()`, `getHurtTime()`, `isOnGround()`
  - `isSprinting()`, `setSprinting()`, `jump()`
  - `getHeldItemId()` - para detección de armas

### Sistema de Módulos (`src/module/`)
- **Module**: Base class con `onEnable()`, `onDisable()`, `onUpdate()`, keybind
- **ModuleManager**: Registro, detección de keybinds (edge detection), tick loop
- **AutoClicker**: 
  - SendMessage clicking (registra en CPS counters)
  - Randomización: drop chance, spike chance, kurtosis
  - Weapon-only mode (swords, axes por ID)
  - Ignore blocks (via MouseOver)

### Web Config (`src/web/`)
- **ConfigServer**: WebSocket server puro WinSock (puerto 8765)
  - Handshake WebSocket
  - Frame encoding/decoding
  - SHA-1 para Accept key
- **Config**: Parser JSON simple, sync bidireccional
  - `apply()`: Recibe config del browser
  - `serialize()`: Envía config actual al browser

## Threading

| Contexto | Thread |
|----------|--------|
| DllMain | Thread del loader |
| Main loop | Thread creado (attached a JVM) |
| WebSocket accept | Thread separado (serverLoop) |
| Llamadas JNI | Solo desde main loop thread |

> **Importante**: `Java::Env` solo es válido en el thread attached. El WebSocket server no hace llamadas JNI directas.

## Build

### Visual Studio
```
Abrir Cobble.sln → Build → Debug/Release x64
Output: bin/Debug/Cobble.dll
```

## Web Config

1. Inyectar DLL en Minecraft
2. Abrir `config/index.html` en navegador
3. Cambios se aplican en tiempo real via WebSocket

## Notas de Estabilidad

- DeleteLocalRef para evitar memory leaks de JNI
- WIN32_LEAN_AND_MEAN antes de WinSock2.h
- Loop principal: 1ms delay para responsividad
- Edge detection en keybinds (no repeat)
- WebSocket reconecta automáticamente

## Known Bugs / TODO

### 🐛 Console close crashea Minecraft
**Descripción**: Cerrar la consola (X o Ctrl+C) termina crasheando Minecraft, aunque el handler retorna TRUE.

**Causa**: La consola está atada al proceso host (Minecraft/Lunar). Windows envía CTRL_CLOSE_EVENT y eventualmente mata el proceso si no termina rápido.

**Soluciones propuestas**:
1. **Sin consola** - Quitar la consola y usar solo Web UI (recomendado)
2. **Inyector separado** - Crear un .exe que mantenga su propia consola e inyecte el DLL. Comunicación via named pipe.

**Workaround actual**: `fastShutdown()` intenta hacer cleanup rápido pero no es 100% efectivo.

