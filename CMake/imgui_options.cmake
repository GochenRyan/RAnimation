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

# Every consumer sees the ImVec2/ImVec4 operators the same way (imgui_internal.h #errors if the macro is
# defined only after imgui.h was included; the node editor and ImGuiFileDialog both define it themselves).
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

add_library(imgui::imgui ALIAS imgui)
