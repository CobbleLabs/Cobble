# Changelog

Todos los cambios notables del proyecto se documentan aquí.

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

---

## [0.16.0] - 2025-12-31

### Added
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

- [ ] Guardar config en JSON file
- [ ] Más módulos: Sprint, Velocity, Reach
- [ ] Mejorar UI web (themes, más módulos)
