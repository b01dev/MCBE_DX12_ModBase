set(IMGUI_SOURCES
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_impl_win32.cpp
    imgui_impl_dx11.cpp
    imgui_impl_dx12.cpp
)

set(IMGUI_HEADERS
    imconfig.h
    imgui.h
    imgui_internal.h
    imgui_impl_win32.h
    imgui_impl_dx11.h
    imgui_impl_dx12.h
)

add_library(imgui STATIC
    ${IMGUI_SOURCES}
    ${IMGUI_HEADERS}
)

target_include_directories(imgui PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)

target_compile_definitions(imgui PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES
    ${IMGUI_SOURCES}
    ${IMGUI_HEADERS}
)

if(MSVC)
    target_compile_options(imgui PRIVATE /utf-8 /FIWindows.h)
endif()
