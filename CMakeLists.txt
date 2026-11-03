cmake_minimum_required(VERSION 3.21)

project(GoldPricePlugin
    VERSION 0.0.1
    DESCRIPTION "Real-time London Gold & ZheShang Gold prices plugin for TrafficMonitor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static runtime library (/MT, /MTd) to avoid vcruntime dependency on user machines
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(${PROJECT_NAME} SHARED
    dllmain.cpp
    GoldPricePlugin.cpp
    GoldPricePlugin.h
    GoldPriceSource.cpp
    GoldPriceSource.h
    PluginInterface.h
    pch.cpp
    pch.h
    framework.h
)

target_precompile_headers(${PROJECT_NAME} PRIVATE pch.h)

target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE
    _UNICODE
    GOLDPRICEPLUGIN_EXPORTS
    _WINDOWS
    _USRDLL
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    wininet
)
