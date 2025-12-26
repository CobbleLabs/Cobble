# Cobble

Injectable client for Minecraft via JNI (Lunar Client compatible).

## Build

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Structure

```
CobbleClient/
├── ext/jni/          # JNI headers and jvm.lib
├── src/
│   ├── main.cpp      # DllMain entry point
│   ├── cobble.cpp    # Core lifecycle
│   ├── jni/          # JVM attachment, classloader
│   └── util/         # Logger
└── CMakeLists.txt
```

## Lifecycle

1. `DllMain` → Creates thread, calls `cobble::start()`
2. `cobble::start()` → Initializes logger, JNI context, enters main loop
3. `cobble::stop()` → Signals shutdown
4. Cleanup → Detaches from JVM, frees console

## Notes

- Requires x64 build (Lunar Client is 64-bit)
- JNI_VERSION_1_8 for compatibility
- ClassLoader found by iterating threads and looking for Minecraft class
