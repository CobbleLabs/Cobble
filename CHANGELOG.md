# Changelog

Todos los cambios notables del proyecto se documentan aquí.

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.0.0/).

---

## [0.2.0] - 2025-12-26

### Added
- Detección de versión de Minecraft (1.7.10 / 1.8.9)
- SDK base: `sdk/version.h`, `sdk/jstring_util.h`
- `Java::GetMinecraftVersion()` lee `launchedVersion` del objeto Minecraft

### Log Output
```
[ + ] detected minecraft 1.8.9
```

---

## [0.1.0] - 2025-12-26

### Added
- Inyección JNI funcional a Lunar Client
- Attach a JVM y obtención de JNIEnv/jvmtiEnv
- ClassLoader discovery para clases de Minecraft
- Sistema de logging a consola con colores
- Estructura base del proyecto (context, loop principal)
- Build system: Visual Studio (.sln) y CMake

### Verified
- Attach exitoso a Lunar Client 1.8.9
- Logs: `got java env`, `attached to java`, `classloader ready`, `cobble initialized`

---

## Próximos Pasos (Planificado)

- [ ] Detección de versión de Minecraft (1.7.10 / 1.8.9)
- [ ] Sistema de módulos/features
- [ ] Configuración persistente
- [ ] SDK de clases de Minecraft
