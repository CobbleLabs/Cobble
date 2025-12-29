# Changelog

Todos los cambios notables del proyecto se documentan aquí.

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

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
- **AimAssist: Reescritura completa** basada en Dream-Advanced
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
