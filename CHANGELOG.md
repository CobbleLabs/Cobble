# Changelog

Todos los cambios notables del proyecto se documentan aquí.

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

---

## [Unreleased]

### Added
- **SDK Refactoring**: Modularización completa del SDK de Minecraft
  - Nuevo **JNI Cache** (`sdk/jni/jni_cache.h/cpp`): Cache centralizado de todos los IDs JNI para mejor performance
  - **Módulos separados** en `sdk/mc/`:
    - `input.cpp`: Keyboard, mouse grab, movement keys
    - `world.cpp`: getBlockId, rayTraceBlocks
    - `render.cpp`: getRenderPos, getPartialTicks, targeting detection
    - `inventory.cpp`: getInventorySlots, windowClick, open/close inventory
    - `player_list.cpp`: getPlayerList, canEntityBeSeen, team color detection
  - `minecraft.cpp` reducido de ~1850 líneas a ~130 líneas (solo core)
- PacketInterceptor JVMTI bytecode hook for `NetworkManager.sendPacket` with native PacketHook bridge.
- GlobalRef caching for packet classes and ClassLoader to keep JNI references stable.
- Drain scheduler and `PacketInterceptor::Tick()` for batched flush of queued packets.
- Overlay context guard with reinit cooldown and target window/context pinning.
- **AimAssist Weapon Only**: New setting to only activate aim assist when holding swords or axes (ported from AutoClicker).
- **Friends/Teams System**: New anti-targeting system for friendly players:
  - Friends list: Add IGNs to ignore (case-insensitive matching)
  - Team detection: Auto-detect teammates by GREEN/DARK_GREEN nametag colors
  - Match own nametag: Players with same color as you are considered teammates
  - Configurable filtering: Apply to Combat and/or Visual modules
  - Full config persistence via cloud sync
- **Complete Config Sync**: All module settings now persist to cloud sync:
  - AimAssist: Added `weaponOnly`, `lockAim`, `reactionTime`
  - Velocity: Added all 14 settings (fov, reduceDelay, jumpChance, conditions, etc.)
  - Reach: Added all 7 settings (distance, randomize, conditions)
  - Blink: Added all 3 settings (maxDuration, disableOnAttack, showDebug)
  - AutoRefill: Added all 3 settings (refillSoup, refillPotions, speed)
- LocalPlayer: prev-rotation accessors (`prevRotationYaw`/`prevRotationPitch`) for smoother rotation updates.

### Changed
- Blink now uses PacketInterceptor API (no `lastReportedPos` mutation).
- ModuleManager calls `PacketInterceptor::Tick()` every update tick.
- Overlay reinit path skips rendering when `wglMakeCurrent` fails.
- AimAssist rotation updates now sync prev-rotation and last-reported yaw/pitch for smoother feel.

### Fixed
- Reduced overlay context churn and GL error spam during frequent context changes.

## [0.20.1] - 2026-01-02

### Fixed
- **AimAssist FPS Drops & Crashes**: Eliminado `SetCursorPos()` que causaba blocking I/O en ciertos sistemas
  - **Lógica original preservada 100%**: Todos boost timers, reaction time, pitch randomization mantienen el mismo feel
  - **Acumulador fraccional**: Reemplaza `SetCursorPos` simulando exactamente su comportamiento de acumulación sub-pixel
  - Rotación  - Migrado a rotación JNI vía `player.setYaw()` / `player.setPitch()`
  - **Hybrid Tracking System**:
    - **Fast Path (144Hz+)**: Tracking directo de posición vía GlobalRef cacheado (sin escaneo de lista)
    - **Slow Path (40Hz)**: Escaneo de `getPlayerList` limitado a 40 veces/seg para eliminar overhead JNI
  - **Zero-Lag Aim**: Eliminado cualquier stutter causado por `SetCursorPos` o spam de JNI
  - Calibración de velocidad restaurada a valores originales ImGui
  - El cursor ya no se mueve durante uso del menú
- **GUI Input Conflicts**: Mismo fix de AimAssist elimina race condition con ImGui
  - El cursor ya no se mueve durante uso del menú
- **Loader Resize Issues**: Console dimensions ahora son DPI-adaptive
  - Detecta DPI del sistema (100%, 125%, 150%+)
  - Dimensiones adaptativas: 84x28 (normal), 77x26 (medium DPI), 70x24 (high DPI)
  - Fallback automático a 80x25 si el resize falla
  - Validación post-resize para confirmar éxito
  - **Por qué solo afectaba a ciertos usuarios**: Windows DPI scaling > 100%, custom console fonts, o Windows Terminal vs ConHost

### Changed
- AimAssist: Ahora usa rotaciones directas del jugador en vez de manipular el cursor del sistema
  - Más "legit" y menos detectable por anti-cheats
  - Eliminada dependencia de `Windows.h` en AimAssist

---

## [0.20.0] - 2026-01-02

### Added
- **Config Persistence**: Sistema híbrido de persistencia de configuración
  - Cache local (`%APPDATA%/Cobble/config.json`) para startup instantáneo
  - Sync server-side opcional para cargar config en cualquier PC
  - Debounce 400ms para evitar spam de requests al cambiar settings
  - Guarda automáticamente al cerrar menú (INSERT o ESC)
  
- **Config Server** (license-server):
  - Nueva tabla `user_configs` para almacenar config por usuario
  - `GET /api/config` - obtener config guardada (auth por HWID)
  - `POST /api/config/save` - guardar config (auth por HWID)
  - Migration 5: `active_sessions` ahora UNIQUE por username (no más duplicados)
  
- **Nuevos Utilities**:
  - `src/util/json.hpp` - JSON parser/serializer extendido
  - `src/util/http.hpp` - Cliente HTTP simple (WinHTTP)
  - `src/util/hwid.hpp` - Generador de HWID (copiado de loader)
  - `src/config/config.hpp` - ConfigManager singleton

- **Settings UI**:
  - Toggle "Cloud Sync" para habilitar/deshabilitar sync (deshabilitado por default)
  - Fallback de IP para conexiones al server (172.235.136.152)

- **Autoupdater System (Integrated)**:
  - **Zero-dependency**: No requiere `updater_stub.exe` separado. El loader se actualiza a sí mismo.
  - **Seamless**: Detecta update → descarga → reinicia solo (sin interacción si es mandatory).
  - **Robust**: Verificación SHA256 + Tamaño + Ed25519 signature.
  - **Cross-Drive Support**: Soporta mover updates entre discos (C: temp → D: bin) automáticamente.
  - **UX**: Update check antes del loading bar para feedback inmediato.
  - **Fail-safe**: Rollback automático si la nueva versión falla al iniciar.
  - Server endpoints: `/api/update/manifest`, `/loader`, `/dll`.
  - Discord commands: `/release`.
  - Documentación completa: `docs/UPDATE_WORKFLOW.md`.

### Changed
- **Startup Flow**: ConfigManager se inicializa después de registrar módulos
- **Shutdown Flow**: Config se guarda antes de deshabilitar módulos
- **Loader**: Update check después de license verification

### Security
- `experimentalUnlocked` ya NO se guarda en config - siempre requiere código
- Manifest firmado Ed25519 protege contra MITM
- Anti-downgrade via `highest_seen` (solo en updates mandatory)

---

## [0.19.0] - 2026-01-01

### Added
- **Locked Modules System**: Sistema de módulos experimentales bloqueados
  - Velocity, Reach, y Blink marcados como `locked = true`
  - UI in-page de desbloqueo con input de código (no popup)
  - `Module::experimentalUnlocked` flag global para desbloquear todos
  - Módulos bloqueados aparecen en gris en el sidebar

### Changed
- **Menu Size**: Aumentado de 670x478 a 750x530 para mejor visibilidad en 1080p
- **Menu Scaling**: Escala mínima aumentada de 0.6 a 0.85
- **Blink Module**: Reescrito para usar WinsockHooks (NtDeviceIoControlFile) en vez de JNI
  - El approach JNI (lastReportedPos manipulation) NO funcionó en Lunar Client
  - El approach actual usa hooks de red a nivel bajo para bloquear paquetes

---

### 📋 Problemas con Blink y Soluciones Futuras

**Intentos fallidos para Blink:**

1. **JNI lastReportedPos** (NO FUNCIONÓ en Lunar)
   - Manipulación de campos `lastReportedPosX/Y/Z` en EntityPlayerSP
   - Lunar Client tiene estos campos ofuscados o protegidos
   - El cliente seguía enviando paquetes de movimiento

2. **WSASend/send hooks** (PARCIAL)
   - Minecraft Java usa Java NIO, no Winsock directamente
   - Los hooks de WSASend capturaban 0 paquetes

3. **NtDeviceIoControlFile hook** (ACTUAL - FUNCIONA)
   - Hook a nivel de kernel/driver intercepta IOCTL de red
   - Bloquea TODO el tráfico saliente (no solo movimiento)
   - Problema: El servidor detecta falta de keepalives después de ~5s

**Soluciones futuras recomendadas:**

1. **JVMTI/Bytecode Injection** (MEJOR)
   - Instrumentar `EntityPlayerSP.onUpdateWalkingPlayer()` via JVMTI
   - Interceptar antes de que se envíe el paquete
   - Requiere más investigación de bytecode Java

2. **JNI con mapping correcto**
   - Obtener mappings actuales de Lunar Client
   - Los campos tienen nombres como `field_175172_bI` (SRG) u otros

3. **Packet-level filtering**
   - Hookear `NetworkManager.sendPacket()` via JNI
   - Filtrar solo C03PacketPlayer y subclases
   - Mantener keepalives funcionando

---

## [0.18.1] - 2026-01-01

### Added
- **ESP 3D Box**: Nueva opción de wireframe 3D que proyecta las 8 esquinas del hitbox del jugador
  - Dibuja las 12 aristas del cubo 3D real (0.6x1.8 bloques)
  - **Face-based filling**: Relleno por caras individuales con front-face culling
  - Círculos en las esquinas para look suavizado
  - Outline opcional con líneas más gruesas
  - Funciona correctamente desde cualquier ángulo (arriba, abajo, lados)
  - Mutuamente exclusivo con Box 2D (solo uno puede estar activo)

- **Blink Module**: Módulo de combat con position freeze via JNI
  - **lastReportedPos manipulation**: Manipula campos privados de EntityPlayerSP para congelar posición
  - **Sin packet interception**: No requiere hookear WSASend ni NetworkManager (funciona en Lunar)
  - **Debug visual**: Box roja (server pos) vs box verde (local pos)
  - **Max Duration**: Timer de seguridad auto-desactiva después de N segundos
  - **Disable on Attack**: Se desactiva automáticamente al atacar
  - Métodos SDK: `captureBlinkPosition()`, `setLastReportedPos()`, `restoreLastReportedPos()`

---

## [0.18.0] - 2025-12-31

### Changed
- **AimAssist**: Reescritura complet
  - Sistema `AimBoost` para randomización de velocidad con interpolación suave
  - Modos de target: Health, Distance, FOV (closest to crosshair)
  - `ApplyAimRand` con pitch randomization cuando tracking horizontal
  - `GetAimPoint` con detección de hitbox para evitar sobre-corrección
  - Movimiento via `SetCursorPos`
  - Opciones: Always Aim, Horizontal Only, Lock Aim, Break Blocks, Aim Closest Point, Target Invisibles
  - Reaction Time configurable
  - Timer class incluida para timing preciso

---

## [0.17.0] - 2025-12-31

### Fixed
### Added
- **Bridge Assist**: Nuevo módulo en la pestaña Movement.
  - Auto-sneak al detectar borde de bloque (bridging seguro).
  - Opciones `Pitch Check` (solo mirando abajo) y `Blocks Only` (solo con bloques en mano).
  - Implementado extendiendo el SDK para leer IDs de bloques (`getBlockId`).
- **Input Handling**: Fixed persistent movement issues where WASD keys weren't restored after closing the menu.
- **Log Spam**: Resolved issue with repeated `releaseMovementKeys` logging.
- **Sprint Persistence**: Fixed sprint being cancelled when opening the menu.
- **Alt+F4**: Fixed window closing handling when menu is open.
- **AutoClicker**: Work in inventory setting now correctly limited to container GUIs (chests, etc), not pause menu.
- **AimAssist**: Fixed aiming while menu is open.

### Changed
- **Keybinds**: Added support for mouse side buttons (XBUTTON1/XBUTTON2).
- **Debug**: Disabled debug console in release builds.

## [0.16.0] - 2025-12-31

### Added
- **ESP Module**: Visuals overlay drawing boxes around players.
  - Options: Box, Filled Box, Outline, Health Bar.
  - Fully configurable colors (Box, Fill, Outline).
  - Configurable range/distance.
  - **[NEW] Smooth/Rounded Design**: Enhanced rounding (8px) for box/fill and health bar (4px) for a soft, pill-like aesthetic.
  - **[NEW] 2D Box Projection**: Switched to Head/Feet projection to eliminate distortion when close to players or looking up/down.
  - **[NEW] Smart Rendering**: ESP automatically pauses when in menus/loading screens to prevent ghosting or overlay issues.
  - Default colors set to Red/Transparent Red.
- **Injection Detection**: Switched to Named Mutex (`Global\Cobble_Instance_{PID}`) for 100% reliable double-injection prevention.
- **Loader Redesign V5 (Premium)**: "Cinematic" UX with Horizontal Gradient banner (UTF-8 fixed), Ultra Smooth Loading Bar (Solid Track + sub-pixel 8x), Fixed 80x30 Window (Centered), Spinner animations, Elegant error handling, and subtle Audio feedback.
- **Modular UI Architecture**: Nueva estructura de directorios `src/ui/` con componentes separados
  - `src/ui/core/` - Sistema base de overlay y hooks
  - `src/ui/widgets/` - Widgets reutilizables consolidados
  - `src/ui/menu/` - Framework del menú principal
  - `src/ui/hud/` - Elementos HUD (ArrayList, Notifications)
- **Module Self-Rendering**: Cada módulo ahora define su propia UI via `onRenderMenu()`
  - Eliminada la necesidad de centralizar UI en `menu.cpp`
  - Módulos son completamente auto-contenidos
- **Layout Helpers**: Nuevas funciones para crear UIs consistentes
  - `ui::two_column()` - Layout de dos columnas estándar
  - `ui::section()` - Sección con header
  - `ui::full_width()` - Sección de ancho completo
- **Consolidated Widgets**: Widgets duplicados ahora centralizados en `widgets.cpp`
  - Checkbox, SliderFloat/Int, Binder, CheckboxBind, ColorPicker
  - Sistema de animación unificado
  - Scaling automático por resolución

### Changed
- **Module base class**: Agregado `virtual void onRenderMenu()` para self-registration
- **Build configuration**: vcxproj actualizado con nuevos paths en `src/ui/`
- **Directory structure**: UI refactorizada de `src/overlay/` a `src/ui/`

### Fixed
- **Menu animation desync**: Widgets nativos de ImGui no se desvanecían sincronizados con widgets custom al cerrar el menú
  - Causa: `ui::alpha_mod` no incluía `ui::anim * ui::content_anim` para el contenido
  - Fix: Sincronizado `ui::alpha_mod` con el alpha combinado antes de renderizar contenido en `menu.cpp`

### Changed
- **ArrayList HUD redesign**: Mejorada estética y escalado proporcional
  - Usa `ui::s()` para escalado por resolución (se ve igual en todas las pantallas)
  - Fuente Bold (font index 3) consistente con la GUI
  - Tamaño más grande: padding 12px, altura 24px, barra 3px (escalados)
  - Esquinas redondeadas en lado izquierdo
  - Color de fondo oscuro RGB(16,16,16) matching GUI
  - Gap de 2px entre items

---

## [0.15.0] - 2025-12-30

### Fixed
- **Critical: Injection crash resolved**:
  - Switched from Manual Mapping to **LoadLibrary injection** (fixes crash on inject)
  - Manual map had TLS/exception handler issues causing immediate crash
  - Process Hacker validation confirmed DLL works perfectly with LoadLibrary
- **Static Initialization Order Fiasco**:
  - All modules (`AutoClicker`, `AimAssist`, `Velocity`, etc.) now use **dynamic allocation**
  - `ModuleManager` and modules are `new`'d at runtime, not as global objects
  - Prevents crashes during DLL load before CRT is fully initialized
- **Proper module cleanup**: All modules properly `delete`d in `shutdown()`

### Added
- **Debug Console Toggle** (`logger.hpp`):
  - `#define DEBUG_CONSOLE 1` to enable colored console for development
  - `#define DEBUG_CONSOLE 0` (default) for production builds (no console, zero overhead)
  - When disabled, all `log_*` macros compile to no-ops
- **Loader improvements**:
  - Disabled `/GS` and `/sdl` security checks for loader (incompatible with shellcode)
  - Graceful handling of failed import loading in manual mapper
  - Temp DLL auto-cleanup after injection

### Changed
- **Loader injection method**: Now uses `CreateRemoteThread(LoadLibraryW)` instead of manual mapping
- **ARCHITECTURE.md**: Updated with new execution flow and module lifecycle

---

## [0.14.0] - 2025-12-30

### Security
- **Hardened Loader**:
  - Eliminado "DEV mode" insecure: ahora solo inyecta desde memoria embebida
  - Manual Mapper aplica protecciones de memoria correctas (RX/RW) post-fase de mapeo (adiós RWX)
  - Validación de status code HTTP y timeouts en checks de licencia
  - URL encoding para parámetros de HWID y Version
  - **[NEW] DLL integrity verification**: SHA256 hash + size check antes de inyectar
  - **[NEW] Automated build pipeline**: `build.ps1` genera hashes automáticamente
- **Hardened License Server**:
  - Middleware de **Rate Limiting** (prevención de abuse/DDoS simple)
  - Soporte real para **X-Forwarded-For** y parsing correcto de IPv6
  - Importado driver `lib/pq` para soporte PostgreSQL nativo
  - Timeouts estrictos en `http.Server` (Read/Write/Idle)

### Changed
- **Loader**: Reemplazado JSON parsing manual (`string.find()`) por parser robusto (`json::Object`)
- **Build**: Nuevo script `build.ps1` que compila DLL → calcula hash → genera header → compila loader

### Fixed
- **Stability**:
  - Corregido memory leak crítico en JNI `AssignClass` (referencias locales no liberadas)
  - Corregido null pointer dereference en shellcode (`libraryLoader`)
- **Anti-Debug**:
  - Mejorada detección de Hardware Breakpoints usando `RtlCaptureContext`

---

## [0.13.0] - 2025-12-30

### Added
- **Velocity Module**: Sistema completo de reducción de knockback
  - **4 Modos**:
    - Regular: Reduce motion al porcentaje especificado
    - Jump: Salta automáticamente para negar knockback (vanilla mechanic abuse)
    - Reduce: Abusa de attack slowdown para reducir KB
    - Ignore: Cancela velocity completamente sin perder velocidad
  - **Configuración avanzada**:
    - Horizontal/Vertical: 0-100% control de knockback
    - Chance: Probabilidad de aplicar reducción
    - FOV: Solo reducir KB de ciertos ángulos (180° = solo frente)
    - Reduce Delay: Ticks antes de aplicar reducción
  - **Modo Ignore - Vertical**:
    - Never: Siempre aplica velocidad vertical
    - Only in Air: Cancela solo si hit en el aire
    - Always: Siempre cancela velocidad vertical
  - **Condiciones**:
    - Looking at Player
    - Mouse Pressed
    - Moving Forward
    - Holding Weapon
    - Sprinting (Jump mode only)
- **NoJumpDelay Module**: Elimina delay de salto para bhop
  - Mantener SPACE para auto-jump continuo
  - Fuerza estado onGround + jump cuando no está en el suelo
- **Movement Tab**: Nueva categoría en el menú para módulos de movimiento
- **Stable CPS Mode**: AutoClicker ahora tiene modo sin randomización
  - Checkbox "Stable CPS" en la UI
  - CPS exactos sin variación cuando está activado
- **Mouse Button Binds**: Soporte completo para MB4 y MB5 (botones laterales del mouse)
  - Funciona en Binder y CheckboxBind widgets
- **ArrayList HUD**: Lista de módulos activos en pantalla
  - Muestra módulos habilitados en esquina superior derecha
  - Toggleable desde Settings → HUD
  - Opciones: Background, Sort by Length
  - Usa color de acento del tema
- **Notifications Toggle**: Las notificaciones ahora se pueden desactivar
  - Checkbox en Settings → HUD
- **Reach Module** (WIP): Módulo de alcance extendido
  - UI completa con configuración de distancia (3.0-6.0)
  - Modo randomización con min/max
  - Condiciones: Weapons Only, Sprinting Only, Moving Only
  - ⚠️ **Nota**: Falta implementar el hook de raycast para que funcione

### Removed
- **HitSelect Module**: Eliminado completamente (reemplazado por Velocity)

### Changed
- Combat tab ahora tiene 5 subtabs: AutoClicker, AimAssist, AutoRefill, Velocity, Reach
- Settings tab movido al índice 3 (nuevo Movement tab en índice 2)

---

## [0.12.0] - 2025-12-30

### Added
- **HitSelect Module**: Filtra clicks innecesarios para mejor knockback en PvP
  - **Burst Mode**: Cancela clicks cuando el target tiene invulnerabilidad (i-frames)
  - **Criticals Mode**: Solo permite hits mientras caes (crits automáticos)
  - Fake Swing: muestra animación de swing aunque se cancele el click
  - Cancel rates configurables para combate y swings al aire
- **HitSelect UI**: Panel completo en Combat tab con selector de modo
- **Humanización anti-detección**:
  - Human Error Rate: chance de "fallar" y dejar pasar hits malos (más realista)
  - Timing Randomization: variación aleatoria en timing de cancelación
  - Defaults conservadores: 65% cancel rate, 8% error rate, 50ms timing variance
  - Thresholds más lenientes (hurtTime > 1 en vez de > 0)

### Changed
- Combat tab ahora tiene 4 subtabs: AutoClicker, AimAssist, AutoRefill, HitSelect
- Config por defecto segura para uso sin baneo

---

## [0.11.1] - 2025-12-30

### Fixed
- **Re-inyección funcional**: Después de eject, el loader ahora puede volver a inyectar
  - La signature `COBBLE_LOADED_V1` se borra durante shutdown
  - Antes persistía en memoria (manual-map no libera) y el loader la detectaba

---

## [0.11.0] - 2025-12-30

### Added
- **CheckboxBind Widget**: Checkbox + Binder combinados en la misma línea
  - `[✓] Enabled        | R |`
  - Click en checkbox para toggle, click en binder para asignar tecla
  - ESC para unbindear
- **Binders en todos los módulos**: AutoClicker, AimAssist, AutoRefill, NameTags

### Changed
- **Colores exactos de weave free**: Tema completamente negro
  - WindowBg: RGB(16,16,16) - casi negro
  - ChildBg: RGB(20,20,20)
  - FrameBg: RGB(16,16,16)
  - Buttons/Headers: RGB(26,26,26)
  - Border sutil blanco al 6%
- **Color de acento**: Naranja RGB(215,96,20) como en weave original
- **Fuentes aumentadas y bold**: 
  - Todas las fuentes ahora usan Segoe UI Bold
  - Tamaños aumentados: 18px/22px (antes 16px/20px)
- **Icono de Combat**: Cambiado de skull (?) a crosshairs
- **Animación de menú**: Fade-in/out suave al abrir/cerrar
- **Mejor espaciado en navegación**: 
  - Más espacio entre el brand "Cobble" y los tabs
  - Padding superior en el area de tabs
- **Sistema de escalado de UI por resolución**:
  - La UI se escala automáticamente según la resolución de pantalla
  - Referencia: 1080p = 100%, 1440p = 133%, 720p = 67%
  - Se ve igual en cualquier monitor (24", 27", etc.)
  - Todos los widgets, tabs, padding y offsets usan escalado

### Fixed
- **Animación de cierre sincronizada**: Arreglado el bug donde los headers y bordes no desvanecían al cerrar el menú
- **Binder input mejorado**: 
  - Agregado delay para evitar capturar el click inicial
  - Mejor detección de hitbox y clicks
  - Comportamiento consistente entre CheckboxBind y Binder standalone
- **Slider que seguía moviendose**: Arreglado el bug donde el slider continuaba animando después de soltar el mouse

---

## [0.10.0] - 2025-12-29

### Added
- **Weave Free UI**: Port completo del estilo visual "weave free" (DX9) a OpenGL2
  - Nuevo sistema de tabs con iconos FontAwesome y animaciones fluidas
  - Headers decorados en child windows con separador inferior
  - Subtabs colapsables con flecha rotativa animada
  - Header principal mostrando tab actual + subtab con flecha
  - Animaciones de fade/slide en cambio de tabs
  - Color picker widget estilo weave
  - Esquema de colores pink/scheme dinámico
  - **Custom Checkbox**: caja animada con checkmark y relleno scheme
  - **Custom SliderFloat/Int**: barra thin con grabber circular, botones +/- animados, label arriba

### Changed
- **Estructura del menú completamente reescrita**
  - `weave_ui.hpp/.cpp` - Nuevo sistema UI portado directamente de weave con widgets custom
  - `menu.cpp` - Layout idéntico a weave: navbar izquierdo + content derecho
  - Fuentes reorganizadas: 0=Medium14+FA, 1=Bold14, 2=Medium17, 3=Bold17
  - Roundings reducidos: Window=8px, Child=6px (estilo weave)

---

## [0.9.0] - 2025-12-29

### Added
- **AutoRefill Module** - Mueve automáticamente items del inventario al hotbar
  - Detecta cuándo soup/potion se consume en hotbar
  - Busca reemplazo en inventario principal (slots 9-35)
  - Distinción de tipos de pociones (solo Health o todas)
  - Configurable: delays, velocidad, smart speed
  - Keybind: G (default)
- **SDK: Inventory Operations**
  - `getInventorySlots()`: Lee los 36 slots con item ID, stack size, y potion metadata
  - `windowClick()`: Simula shift-click para mover items
- **Web GUI: AutoRefill Panel**
  - Items: Soup, Potions, Use Non-Health Potions
  - Simulate Button: Left/Right/Both
  - Timing: Delay After Open, Delay Before Close, Speed![alt text](image.png)
  - Smart Speed: delay extra según distancia de slots

---

## [0.8.0] - 2025-12-29

### Added
- **Botón Eject en Web UI**: Desinyecta el cliente limpiamente sin reiniciar Minecraft
  - Nuevo botón en `config/index.html`
  - Comando `eject` via WebSocket
- **Re-inyección funcional**: Después de eject, se puede volver a inyectar el DLL
  - El DLL ahora se descarga completamente con `FreeLibraryAndExitThread`

### Changed
- **Injector UI mejorada**: Diseño limpio y profesional
  - Banner ASCII simple que se ve bien en cualquier consola
  - Ventana compacta y auto-cierre después de 3 segundos
  - Sin eject loop (ahora se usa el botón en web UI)

### Fixed
- **Console crash resuelto**: Cerrar la ventana o Ctrl+C ya NO crashea Minecraft
  - Consola completamente removida del DLL
  - Toda interacción ahora via Web UI (`config/index.html`)
  - Logger ahora es no-op (no aloca consola)
- **Eject crash resuelto**: El botón Eject ya no crashea Minecraft
  - Removido thread detached del overlay
  - Flag de shutdown para detener el hook antes de cleanup
  - Delays para que los hooks terminen antes de descargar el DLL

### Removed
- Console allocation y handlers
- `fastShutdown()` - ya no es necesario sin consola

---

## [0.7.2] - 2025-12-26

### Added
- **Ctrl+C / Cerrar ventana** para descargar el cliente sin cerrar Minecraft
  - Console handler que termina limpiamente el DLL
  - Desactiva módulos antes de descargar
  - Minecraft sigue corriendo normalmente
- Título de consola: "Cobble Client"
- Instrucciones claras al iniciar (R, V, Ctrl+C)

### Changed
- **Consola aislada**: El chat de Minecraft ya no aparece en nuestra consola
  - Logger usa WriteConsole directamente en vez de redirigir stdout
- **Consola sin spam**: Solo loguea enable/disable de módulos
- AutoClicker: Max CPS ahora limitado a 20
- AutoClicker: Validación min ≤ max en sliders

---

## [0.7.1] - 2025-12-26

### Changed
- **AimAssist: Ultra Smooth Mode**
  - Si ya estás apuntando a la hitbox del target, no fuerza rotación (más natural)
  - Nuevo scale de speed 0-10: 0=mínimo, 5=balanceado, 10=rápido pero smooth
  - Exponential decay scaling: desacelera suavemente al acercarse al target
  - Defaults suaves: hSpeed=5, vSpeed=3, FOV=60°
  - Update rate ajustado a 2-8ms para smoothness óptimo
  - Clamp de rotación al 80% del ángulo restante

### Fixed
- Warning de conversión double→float en cálculo de hitbox

---

## [0.7.0] - 2025-12-26

### Changed
- **AimAssist: Reescritura completa**
  - Timer-based updates (1-10ms) para smoothness óptimo
  - Fórmula de sensibilidad de MC: `(sens * 0.6 + 0.2)^3 * 8`
  - Hurt Time Boost: rota más rápido cuando te golpean
  - Target Lock: mantiene el mismo target mientras clickeas
  - Vertical Aim: opcional, ajusta pitch además de yaw
  - Randomization: variación natural en el movimiento
  - Speed scaling por ángulo (ángulos grandes = rotación más rápida)
  - Horizontal/Vertical speed separados

### Added
- Nuevos settings de AimAssist:
  - `horizontalSpeed` / `verticalSpeed` separados
  - `targetLock` - lockea en un target
  - `verticalAim` - ajusta pitch
  - `hurtTimeBoost` - más rápido cuando te pegan
  - `randomization` - variación natural

---

## [0.6.1] - 2025-12-26

### Changed
- **AimAssist: Smoothing mejorado**
  - Interpolación exponencial en lugar de lineal
  - Ajuste dinámico por distancia (más rápido cerca)
  - Ajuste por ángulo (correcciones pequeñas más suaves)
  - Dead zone reducida a 0.5° para precisión
- **AimAssist: Distancia horizontal**
  - Usa solo X,Z para cálculo de distancia (ignora Y)
  - Más preciso para detección de reach en PvP
  - Prioriza targets por distancia, luego por ángulo

---

## [0.6.0] - 2025-12-26

### Added
- **AimAssist Module** - Rotación suave hacia jugadores cercanos
  - Visibility check (solo apunta a jugadores visibles)
  - Ignore invisible players
  - Only when clicking/sprinting options
  - Configurable: speed, FOV, distance
  - Keybind: V (default)
- **SDK: Player List** (`mc.getPlayerList()`)
  - Obtiene todos los jugadores del mundo vía JNI
  - Excluye automáticamente al jugador local
  - Incluye posición e invisibilidad de cada jugador
- **SDK: Visibility Check** (`mc.canEntityBeSeen()`)
  - Wrapper para EntityLivingBase.canEntityBeSeen()
- **Web GUI: AimAssist Panel**
  - Sliders para speed, FOV, distance
  - Checkboxes para visibility, invisible, sprinting, clicking
  - Keybind configurable

### Changed
- Config serialization ahora envía ambos módulos en un solo mensaje (`configAll`)
- Web GUI soporta múltiples módulos independientes

---

## [0.5.0] - 2025-12-26

### Added
- **WebSocket Server** para configuración en tiempo real
  - Puerto 8765, protocolo WebSocket puro (sin deps externas)
  - Sync bidireccional con web GUI
- **Web Config GUI** (`config/index.html`)
  - Sliders para CPS, drop chance, spike chance, kurtosis
  - Toggles y checkboxes para todas las opciones
  - Listener de keybind
  - Reconexión automática
- Keybind configurable via web (A-Z, 0-9, SHIFT, CTRL, ALT, etc)

### Changed
- Loop principal reducido a 1ms para mejor responsividad
- Config se aplica instantáneamente desde web

---

## [0.4.0] - 2025-12-26

### Added
- **MouseOver Wrapper** (`mc.isTargetingBlock()`)
  - Compara `objectMouseOver.typeOfHit == BLOCK`
  - Usado por AutoClicker para "Ignore Blocks"
- **Inventory Wrapper** (`player.getHeldItemId()`)
  - Obtiene ID del item en mano via JNI chain
  - `inventory → getCurrentItem() → getItem() → getIdFromItem()`

---

## [0.3.0] - 2025-12-26

### Added
- **Sistema de Módulos**
  - `Module` base class con toggle, keybind, update tick
  - `ModuleManager` con registro y edge detection de keys
- **AutoClicker Module**
  - SendMessage clicking (registra en CPS counters)
  - Min/Max CPS con randomización
  - Advanced mode: drop chance, spike chance, kurtosis
  - Weapon-only mode (swords ID 268,272,267,276,283 / axes)
  - Ignore blocks, work in inventory

### Changed
- SDK simplificado: wrappers planos sin herencia compleja
- Lazy init de JNI fields para mejor startup

---

## [0.2.0] - 2025-12-26

### Added
- **SDK Base**
  - `Minecraft` wrapper (thePlayer, theWorld, currentScreen)
  - `LocalPlayer` wrapper (posición, movimiento, rotación, health)
- Detección de versión de Minecraft (1.7.10 / 1.8.9)

---

## [0.1.0] - 2025-12-26

### Added
- Inyección JNI funcional a Lunar Client
- Attach a JVM y obtención de JNIEnv/jvmtiEnv
- ClassLoader discovery para clases de Minecraft
- Sistema de logging a consola con colores
- Estructura base del proyecto

---

## Próximos Pasos

- [x] ~~Guardar config en JSON file~~ (v0.20.0)
- [x] ~~Más módulos: Velocity~~ (v0.13.0)
- [ ] Implementar hook de raycast para Reach
- [ ] Blink: migrar a JVMTI o packet-level filtering
- [ ] Mejorar UI web (themes, más módulos)
