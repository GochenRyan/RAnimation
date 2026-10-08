# imgui-node-editor (fork: GochenRyan/imgui-node-editor, branch ranim-imgui-1.92) compiled against the
# Packages/imgui submodule. Explicitly STATIC: BUILD_SHARED_LIBS is cached ON by the assimp options.
set(RANIM_NODE_EDITOR_DIR ${CMAKE_SOURCE_DIR}/Packages/imgui-node-editor)

add_library(imgui_node_editor STATIC
    ${RANIM_NODE_EDITOR_DIR}/crude_json.cpp
    ${RANIM_NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${RANIM_NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${RANIM_NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
)
target_include_directories(imgui_node_editor PUBLIC ${RANIM_NODE_EDITOR_DIR})
target_link_libraries(imgui_node_editor PUBLIC imgui)
set_target_properties(imgui_node_editor PROPERTIES CXX_STANDARD 17 FOLDER "Packages")
set_property(TARGET imgui_node_editor PROPERTY COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    target_compile_options(imgui_node_editor PRIVATE /W3 /WX-)
endif()
