# Cobble - Architecture

Ghost client inyectable para Lunar Client (Minecraft) via JNI.

## Estructura del Proyecto

```
Cobble/
├── src/
│   ├── main.cpp          # Entry point (DllMain)
│   ├── cobble.cpp/.hpp   # Context principal y ciclo de vida
│   ├── jni/
│   │   ├── java.cpp/.h   # Puente JNI y ClassLoader
│   └── util/
│       └── logger.hpp    # Sistema de logging a consola
├── ext/
│   └── jni/              # Headers JNI y jvm.lib
└── bin/                  # DLL compilada
```

## Ciclo de Vida

```
DllMain (DLL_PROCESS_ATTACH)
    └── CreateThread → cobble::context::initialize()
                           ├── logger.initialize()      # Consola
                           ├── Java::Init()             # JNI attach + ClassLoader
                           ├── Main loop (5ms tick)
                           └── shutdown() on exit
```

## Componentes

### `cobble::context`
- **initialize()**: Setup inicial, loop principal
- **shutdown()**: Limpieza ordenada
- **is_running**: Flag de control del loop

### `Java` (JNI Bridge)
- **Init()**: Attach a la JVM, obtiene JNIEnv y JVMTI, configura ClassLoader
- **Kill()**: Detach de la JVM
- **AssignClass()**: Busca clases via ClassLoader de Minecraft
- **Env/tiEnv**: Punteros globales a JNIEnv y jvmtiEnv

### ClassLoader Setup
El ClassLoader se obtiene iterando todos los threads de la JVM y buscando el que puede cargar `net.minecraft.client.Minecraft`. Esto es necesario porque las clases de Minecraft están en un ClassLoader custom, no en el bootstrap.

## Threading

| Contexto | Thread |
|----------|--------|
| DllMain | Thread del loader |
| initialize()/loop | Thread creado por CreateThread |
| Llamadas JNI | Mismo thread del loop (attached) |

> **Importante**: `Java::Env` solo es válido en el thread donde se hizo `AttachCurrentThread`. No usar desde otros threads.

## Build

### Visual Studio (recomendado)
```
Abrir Cobble.sln → Build → Release x64
```

### CMake (alternativo)
```bash
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release
```

## Dependencias

- **jvm.lib**: Import library para JNI_GetCreatedJavaVMs
- **Headers JNI**: jni.h, jni_md.h, jvmti.h

## Agregar Features

1. Crear archivos en `src/` o subcarpeta apropiada
2. Incluir desde `cobble.hpp` si es necesario
3. Inicializar en `cobble::context::initialize()` después de `Java::Init()`
4. Limpiar en `shutdown()` en orden inverso

## Notas de Estabilidad

- No llamar JNI desde threads no-attached
- DeleteLocalRef para evitar leaks de referencias locales
- El loop principal tiene delay de 5ms para no saturar CPU
- Siempre verificar punteros JNI antes de usarlos (pueden ser null)
