cmake_minimum_required(VERSION 3.20)
project(Cobble LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Cobble SHARED
    src/main.cpp
    src/cobble.cpp
    src/cobble.hpp
    src/jni/java.cpp
    src/jni/java.h
    src/util/logger.hpp
    src/sdk/version.h
    src/sdk/jstring_util.h
)

target_include_directories(Cobble PRIVATE
    ${CMAKE_SOURCE_DIR}/ext/jni
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(Cobble PRIVATE
    ${CMAKE_SOURCE_DIR}/ext/jni/jvm.lib
)

target_compile_definitions(Cobble PRIVATE
    WIN32
    _WINDOWS
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

set_target_properties(Cobble PROPERTIES
    OUTPUT_NAME "Cobble"
    SUFFIX ".dll"
)
