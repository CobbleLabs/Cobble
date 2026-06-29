cmake_minimum_required(VERSION 3.20)
project(Pulse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows subsystem (no console window)
if(MSVC)
    add_link_options("/SUBSYSTEM:WINDOWS")
endif()

set(SOURCES
    src/main.cpp
    src/RegistryManager.cpp
    src/NetshUtility.cpp
    src/GameConfigManager.cpp
    src/OptimizationEngine.cpp
)

add_executable(Pulse WIN32 ${SOURCES})

target_include_directories(Pulse PRIVATE include)

target_link_libraries(Pulse PRIVATE
    iphlpapi
    ws2_32
    ole32
    advapi32
    shell32
    gdiplus
    comctl32
    uxtheme
    dwmapi
    user32
    gdi32
    shlwapi
)

# Embed the manifest
if(MSVC)
    set_target_properties(Pulse PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/Pulse.manifest"
    )
endif()
