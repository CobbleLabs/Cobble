# Pulse Network Optimizer — C++ Port

Puerto nativo Win32/C++17 de la aplicación original C# WPF.

## Estructura

```
PulseCpp/
├── CMakeLists.txt
├── Pulse.manifest          # UAC elevation (requireAdministrator)
├── include/
│   ├── RegistryManager.h
│   ├── NetshUtility.h
│   ├── GameConfigManager.h
│   └── OptimizationEngine.h
└── src/
    ├── main.cpp            # Win32 window + UI (GDI+)
    ├── RegistryManager.cpp
    ├── NetshUtility.cpp
    ├── GameConfigManager.cpp
    └── OptimizationEngine.cpp
```

## Compilar (Visual Studio / MSVC)

```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

O desde **Developer Command Prompt**:

```bat
cl /std:c++17 /W3 /O2 /EHsc ^
   /I include ^
   src\main.cpp src\RegistryManager.cpp src\NetshUtility.cpp ^
   src\GameConfigManager.cpp src\OptimizationEngine.cpp ^
   /Fe:Pulse.exe /link ^
   /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:Pulse.manifest ^
   iphlpapi.lib ws2_32.lib ole32.lib advapi32.lib shell32.lib ^
   gdiplus.lib comctl32.lib uxtheme.lib dwmapi.lib user32.lib gdi32.lib shlwapi.lib
```

## Diferencias vs. versión C#/WPF

| Aspecto             | C# (original)          | C++ (este port)           |
|---------------------|------------------------|---------------------------|
| UI framework        | WPF + XAML             | Win32 API + GDI+          |
| Async               | async/await            | std::thread detached      |
| Registry            | Microsoft.Win32         | RegCreateKeyExW / RegSetValueExW |
| Network info        | NetworkInterface (.NET) | GetAdaptersAddresses (WinAPI) |
| File I/O            | System.IO              | std::ifstream / WriteFile |
| Estilos/animaciones | XAML triggers          | GDI+ rendering manual     |
| Runtime             | .NET 8                 | MSVC CRT (sin runtime)    |

## Notas

- Requiere **Windows 10/11** y correr como **Administrador** (el manifest lo pide automáticamente).
- El slider de trackbar usa el control estándar `TRACKBAR_CLASS`; el rendering del fondo del slider es personalizado vía WM_PAINT del padre.
- El toggle "Smart Packets" tiene subclass para renderizado dark custom.
