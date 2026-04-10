add_library(imgui STATIC
    ${CMAKE_SOURCE_DIR}/Packages/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/Packages/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/Packages/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/Packages/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/Packages/imgui/backends/imgui_impl_sdl3.cpp
)

target_include_directories(imgui
    PUBLIC
        ${CMAKE_SOURCE_DIR}/Packages/imgui
        ${CMAKE_SOURCE_DIR}/Packages/imgui/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(imgui::imgui ALIAS imgui)
