cmake_minimum_required(VERSION 3.20)
project(MCBE_DX12_ModBase LANGUAGES CXX)

# Default to Release build for single config generators e.g Ninja if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

file(GLOB_RECURSE MCBE_DX12_MODBASE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE MCBE_DX12_MODBASE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)


# Disable unused extra targets in libhat
set(LIBHAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBHAT_BUILD_CSHARP OFF CACHE BOOL "" FORCE)
set(LIBHAT_STATIC_C_LIB OFF CACHE BOOL "" FORCE)
set(LIBHAT_SHARED_C_LIB OFF CACHE BOOL "" FORCE)

# External subdirectories
add_subdirectory(external/imgui)
add_subdirectory(external/libhat)

set(MCBE_DX12_MODBASE_VENDOR_SOURCES
    external/kiero/kiero.cpp
)

add_library(MCBE_DX12_ModBase SHARED
    ${MCBE_DX12_MODBASE_SOURCES}
    ${MCBE_DX12_MODBASE_HEADERS}
    ${MCBE_DX12_MODBASE_VENDOR_SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES
    ${MCBE_DX12_MODBASE_SOURCES}
    ${MCBE_DX12_MODBASE_HEADERS}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/external" PREFIX "External" FILES
    ${MCBE_DX12_MODBASE_VENDOR_SOURCES}
)

target_include_directories(MCBE_DX12_ModBase PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/external"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/kiero"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/minhook"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/d3d12"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/entt"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/libhat/include" 
)

target_link_directories(MCBE_DX12_ModBase PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/minhook/lib"
)

target_link_libraries(MCBE_DX12_ModBase PRIVATE
    imgui
    Minhook
    d3d12
    dxgi
    Psapi
    libhat::libhat 
)

target_compile_definitions(MCBE_DX12_ModBase PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

set_target_properties(MCBE_DX12_ModBase PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "MCBE_DX12_ModBase"
)

if(MSVC)
    target_compile_options(MCBE_DX12_ModBase PRIVATE 
        /W4 /permissive- /utf-8 /Zc:preprocessor /FIWindows.h /EHa
        $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /Oy /DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:/O2 /Ob2 /Oi /Ot /Oy /Zi /DNDEBUG>
    )
    target_link_options(MCBE_DX12_ModBase PRIVATE
        $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/OPT:REF /OPT:ICF /DEBUG>
    )
endif()
