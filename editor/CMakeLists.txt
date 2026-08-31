set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
set(IMGUIZMO_DIR "${CMAKE_SOURCE_DIR}/third_party/ImGuizmo")
set(NANOSVG_DIR "${CMAKE_SOURCE_DIR}/third_party/nanosvg")

add_executable(DemonEditor
    DemonEditorApp.cpp
    Win32ImGuiIntegration.cpp
    ImGuiEditorLayer.h
    ProcessManager.cpp
    ProcessManager.h
    panels/ConsolePanel.cpp
    panels/ConsolePanel.h
    panels/ContentBrowserWin32.cpp
    panels/ContentBrowserPanel.h
    panels/EnvironmentPanel.cpp
    panels/EnvironmentPanel.h
    panels/MaterialEditorPanel.cpp
    panels/MaterialEditorPanel.h
    panels/PropertiesPanel.cpp
    panels/PropertiesPanel.h
    panels/SceneHierarchyPanel.cpp
    panels/SceneHierarchyPanel.h
    panels/StatsPanel.cpp
    panels/StatsPanel.h
    panels/ViewportPanel.cpp
    panels/ViewportPanel.h
    utils/FileDialogs.cpp
    utils/FileDialogs.h
    widgets/DemonTheme.cpp
    widgets/DemonTheme.h
    widgets/EditorIcons.cpp
    widgets/EditorIcons.h
    "${CMAKE_SOURCE_DIR}/ProjectHubWindow/ProjectHubWindow.cpp"
    "${CMAKE_SOURCE_DIR}/ProjectHubWindow/ProjectHubWindow.h"
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx12.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUIZMO_DIR}/ImGuizmo.cpp"
    DemonEditor.rc
)

add_library(DemonGameModule SHARED GameModule.cpp)
set_target_properties(DemonGameModule PROPERTIES
    OUTPUT_NAME "DemonGameModule"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
)

add_executable(DemonGamePlayer
    GamePlayerApp.cpp
)

target_link_libraries(DemonGamePlayer PRIVATE Demon::Runtime)
target_include_directories(DemonGamePlayer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine"
    "${CMAKE_SOURCE_DIR}"
)
set_target_properties(DemonGamePlayer PROPERTIES
    OUTPUT_NAME "DemonGamePlayer"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
if(MSVC)
    target_link_options(DemonGamePlayer PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

target_link_libraries(DemonEditor PRIVATE Demon::Runtime)
target_link_libraries(DemonEditor PRIVATE comctl32 comdlg32 uxtheme psapi dwmapi)
add_dependencies(DemonEditor
    DemonGameModule
    DemonGamePlayer
    DemonCrashGuard
    DemonShaderCompiler
    DemonAssetWorker
    DemonStreamHost
    DemonThumbGen
    DemonCacheServer
    DemonScriptVM
    DemonPhysicsServer
    DemonBuildAgent)

target_include_directories(DemonEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/engine"
    "${CMAKE_SOURCE_DIR}"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${IMGUIZMO_DIR}"
    "${NANOSVG_DIR}"
)
target_compile_definitions(DemonEditor PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS
    ImGuiWindowFlags_NoDocking=ImGuiWindowFlags_NoDocking
    ImGuiDockNodeFlags_None=ImGuiDockNodeFlags_None
    ImGuiDockNodeFlags_NoDocking=ImGuiDockNodeFlags_NoDocking
    ImGuiDockNodeFlags_NoSplit=ImGuiDockNodeFlags_NoSplit
    ImGuiDockNodeFlags_NoResize=ImGuiDockNodeFlags_NoResize
)

set_target_properties(DemonEditor PROPERTIES OUTPUT_NAME "DemonEngine")

add_custom_command(TARGET DemonEditor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:DemonEditor>/assets"
    COMMENT "[DemonEngine] Deploying editor assets"
)

if(MINGW)
    get_filename_component(DEMON_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(DEMON_RUNTIME_DLL libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        add_custom_command(TARGET DemonEditor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${DEMON_MINGW_BIN_DIR}/${DEMON_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:DemonEditor>/${DEMON_RUNTIME_DLL}"
        )
    endforeach()
endif()

set_source_files_properties(
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx12.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUIZMO_DIR}/ImGuizmo.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
