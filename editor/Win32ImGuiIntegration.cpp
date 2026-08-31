#include "ImGuiEditorLayer.h"
#include "EditorSettings.h"

#include "core/Application.h"
#include "core/Logger.h"
#include "core/ProjectCache.h"
#include "renderer/DX12Context.h"
#include "renderer/DX12DescriptorHeap.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "scripting/ScriptEngine.h"
#include "utils/FileDialogs.h"
#include "widgets/DemonTheme.h"
#include "widgets/EditorIcons.h"
#include "../ProjectHubWindow/ProjectHubWindow.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <ImGuizmo.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <future>
#include <string_view>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Demon {
namespace {

enum : UINT {
    MenuFileNewProject = 1001,
    MenuFileOpenProject,
    MenuFileNew,
    MenuFileOpen,
    MenuFileSave,
    MenuFileSaveAs,
    MenuFileProjectSettings,
    MenuFileBuildSettings,
    MenuFileBuildWindows,
    MenuFileDebugView,
    MenuFileExit,
    MenuEditUndo,
    MenuEditRedo,
    MenuEditCreateEmpty,
    MenuEditDuplicate,
    MenuEditDelete,
    MenuEditSelectAll,
    MenuViewHierarchy,
    MenuViewInspector,
    MenuViewViewport,
    MenuViewContentBrowser,
    MenuViewConsole,
    MenuViewStats,
    MenuViewEnvironment,
    MenuViewMaterialEditor,
    MenuViewLockDocking,
    MenuViewResetLayout,
    MenuToolsCompileShaders,
    MenuToolsHotReloadShaders,
    MenuToolsCompileScripts,
    MenuToolsHotReloadScripts,
    MenuToolsOpenProjectFolder,
    MenuPlayToggle,
    MenuPlayPause,
    MenuHelpAbout,
};

std::filesystem::path executablePath()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

// ---------------------------------------------------------------------------
//  Native Win32 shader-hot-reload popup with a comctl32 progress bar.
// ---------------------------------------------------------------------------
struct ShaderReloadPopup {
    HWND  window = nullptr;
    HWND  label  = nullptr;
    HWND  bar    = nullptr;
    HFONT font   = nullptr;
};

LRESULT CALLBACK shaderReloadWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    // The window is driven programmatically; swallow the close button so the
    // user cannot dismiss it mid-compile and leave the GPU in a half state.
    if (msg == WM_CLOSE)
        return 0;
    if (msg == WM_CTLCOLORSTATIC) {
        SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    return DefWindowProcW(h, msg, w, l);
}

void pumpUiMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

ShaderReloadPopup createShaderReloadPopup(HWND parent)
{
    static const wchar_t* kClassName = L"DemonShaderReloadPopup";
    static bool registered = false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = shaderReloadWndProc;
        wc.hInstance     = instance;
        wc.hCursor       = LoadCursor(nullptr, IDC_APPSTARTING);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    const int width = 480;
    const int height = 158;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    RECT pr{};
    if (parent && GetWindowRect(parent, &pr)) {
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    ShaderReloadPopup popup;
    popup.window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kClassName,
        L"DemonEngine  —  Shader Hot Reload",
        WS_POPUP | WS_CAPTION | WS_VISIBLE,
        x, y, width, height, parent, nullptr, instance, nullptr);
    if (!popup.window)
        return popup;

    popup.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    popup.label = CreateWindowExW(0, L"STATIC", L"Preparing shader hot reload...",
        WS_CHILD | WS_VISIBLE, 22, 22, width - 64, 28, popup.window, nullptr, instance, nullptr);
    popup.bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 22, 62, width - 64, 26,
        popup.window, nullptr, instance, nullptr);

    if (popup.font)
        SendMessageW(popup.label, WM_SETFONT, reinterpret_cast<WPARAM>(popup.font), TRUE);
    SendMessageW(popup.bar, PBM_SETRANGE32, 0, 100);
    SendMessageW(popup.bar, PBM_SETPOS, 0, 0);
    UpdateWindow(popup.window);
    pumpUiMessages();
    return popup;
}

void updateShaderReloadPopup(ShaderReloadPopup& popup, const wchar_t* text, int percent)
{
    if (popup.label && text)
        SetWindowTextW(popup.label, text);
    if (popup.bar)
        SendMessageW(popup.bar, PBM_SETPOS, static_cast<WPARAM>(percent), 0);
    pumpUiMessages();
}

void destroyShaderReloadPopup(ShaderReloadPopup& popup)
{
    if (popup.window)
        DestroyWindow(popup.window);
    if (popup.font)
        DeleteObject(popup.font);
    popup = {};
}

// Runs DemonShaderCompiler.exe synchronously with explicit source/output dirs so
// the freshly built .cso files land exactly where the running renderer loads
// them. Returns true only when the compiler exits 0.
bool runShaderCompilerProcess(const std::filesystem::path& compiler,
                              const std::filesystem::path& shaderSource,
                              const std::filesystem::path& outputDir)
{
    if (!std::filesystem::exists(compiler)) {
        DEMON_LOG_ERROR("Shader hot reload: compiler not found at {}", compiler.string());
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    std::wstring commandLine = L"\"" + compiler.wstring() + L"\""
        + L" --dir \"" + shaderSource.wstring() + L"\""
        + L" --out \"" + outputDir.wstring() + L"\"";

    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring workingDir = compiler.parent_path().wstring();
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr,
                        workingDir.empty() ? nullptr : workingDir.c_str(),
                        &si, &pi)) {
        DEMON_LOG_ERROR("Shader hot reload: failed to launch DemonShaderCompiler.exe");
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode == 0;
}

void addDefaultSceneEntities(const std::shared_ptr<Scene>& scene)
{
    if (!scene || !scene->getEntities().empty())
        return;

    auto camera = scene->createEntity("Camera");
    camera.addComponent<TransformComponent>().translation = {0.0f, 2.5f, 6.0f};
    camera.addComponent<CameraComponent>();

    auto light = scene->createEntity("Directional Light");
    light.addComponent<TransformComponent>().rotation = {-45.0f, 45.0f, 0.0f};
    auto& lightComponent = light.addComponent<LightComponent>();
    lightComponent.type = LightType::Directional;
    lightComponent.intensity = 2.0f;

    auto cube = scene->createEntity("Cube");
    cube.addComponent<TransformComponent>().translation = {0.0f, 0.5f, 0.0f};
    cube.addComponent<MeshRendererComponent>().meshPath = "builtin:cube";
    cube.addComponent<MaterialComponent>();
}

bool isDemonSceneFile(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension == ".demonscene";
}

void showInvalidDemonSceneFileError()
{
    MessageBoxA(Application::get().getWindow().getWin32Handle(),
                "DSF001: The selected Demon Scene File is invalid",
                "Open Scene",
                MB_ICONERROR);
}

std::string wideToUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8,
                                         0,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr,
                                         0,
                                         nullptr,
                                         nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        result.data(),
                        size,
                        nullptr,
                        nullptr);
    return result;
}

} // namespace

ImGuiEditorLayer::ImGuiEditorLayer()
    : Layer("ImGuiEditorLayer")
{
}

ImGuiEditorLayer::~ImGuiEditorLayer() = default;

void ImGuiEditorLayer::onAttach()
{
    auto& app = Application::get();
    auto& renderer = app.getRenderer();
    auto& context = renderer.getContext();
    auto& srvHeap = renderer.getSrvHeap();

    SetCurrentProcessExplicitAppUserModelID(L"CodeStudioGames.DemonEngine");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "DemonEditorLayout.ini";

    DemonTheme::apply();

    ImGui_ImplWin32_Init(app.getWindow().getWin32Handle());
    ImGui_ImplDX12_InitInfo dx12Info{};
    dx12Info.Device = context.getDevice();
    dx12Info.CommandQueue = context.getCommandQueue();
    dx12Info.NumFramesInFlight = static_cast<int>(DX12Context::k_frameCount);
    dx12Info.RTVFormat = renderer.getSwapchainFormat();
    dx12Info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    dx12Info.UserData = &srvHeap;
    dx12Info.SrvDescriptorHeap = srvHeap.get();
    dx12Info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle) {
        auto* heap = static_cast<DX12DescriptorHeap*>(info->UserData);
        const uint32_t descriptorIndex = heap->allocate(1);
        *cpuHandle = heap->cpuHandle(descriptorIndex);
        *gpuHandle = heap->gpuHandle(descriptorIndex);
    };
    dx12Info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                                      D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE) {
        // The renderer owns a frame-lifetime linear descriptor heap.
    };
    const bool dx12Ready = ImGui_ImplDX12_Init(&dx12Info);
    DEMON_ASSERT(dx12Ready, "Failed to initialize Dear ImGui DX12 backend");
    DemonTheme::applyFont(15.0f * app.getDpiScale());
    EditorIcons::init();

    app.getWindow().setNativeMessageCallback([this](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
        -> std::optional<LRESULT> {
        if ((message == WM_SYSKEYDOWN && wParam == VK_F4) ||
            message == WM_CLOSE)
        {
            if (m_playMode) {
                exitPlayMode();
                setNativeStatus(L"Runtime mode exited", 100, 2.0f);
                return 0;
            }
        }
        if (message == WM_KEYDOWN) {
            const bool textInput = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            UINT command = 0;
            if (!textInput && ctrl) {
                switch (wParam) {
                    case 'N': command = MenuFileNew; break;
                    case 'O': command = MenuFileOpen; break;
                    case 'S': command = shift ? MenuFileSaveAs : MenuFileSave; break;
                    case 'Z': command = shift ? MenuEditRedo : MenuEditUndo; break;
                    case 'Y': command = MenuEditRedo; break;
                    case 'D': command = MenuEditDuplicate; break;
                    case 'A': command = MenuEditSelectAll; break;
                    default: break;
                }
            } else if (!textInput) {
                if (wParam == VK_DELETE) command = MenuEditDelete;
                else if (wParam == VK_F5) command = MenuPlayToggle;
                else if (wParam == VK_F6) command = MenuPlayPause;
            }
            if (command != 0) {
                handleNativeMenuCommand(command);
                return 0;
            }
        }
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam))
            return 1;
        return std::nullopt;
    });

    ensureSceneReady();
    const auto editorExecutable = executablePath();
    const auto processLogDir = m_projectRoot.empty()
        ? editorExecutable.parent_path() / "demon_logs"
        : m_projectRoot / "demon_logs";
    (void)m_processManager.startPersistentProcesses(editorExecutable.parent_path(), processLogDir);
    m_processManager.updateHealth();
    setNativeStatus(m_processManager.statusLine(), 100, 0.0f);
    DEMON_LOG_INFO("ImGuiEditorLayer: Win32 and DX12 backends initialized.");
}

void ImGuiEditorLayer::onDetach()
{
    m_viewport.setFullscreen(false);
    m_processManager.shutdown();

    auto& window = Application::get().getWindow();
    window.setNativeMessageCallback({});

    if (ImGui::GetCurrentContext()) {
        Application::get().getRenderer().getContext().prepareForResize();
        EditorIcons::shutdown();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    DEMON_LOG_INFO("ImGuiEditorLayer: shutdown complete.");
}

void ImGuiEditorLayer::onUpdate(float dt)
{
    ensureSceneReady();
    auto scene = getActiveScene();
    if (!scene)
        return;

    m_viewport.onUpdate(dt, scene);
    if (m_viewport.consumePauseToggleRequested())
        m_paused = !m_paused;
    if (!m_playMode || !m_paused)
        scene->onUpdate(dt);

    static float scriptPollTimer = 0.0f;
    scriptPollTimer += dt;
    if (scriptPollTimer >= 0.75f) {
        scriptPollTimer = 0.0f;
        if (m_projectSettings.scripting.hotReloadEnabled &&
            m_projectSettings.scripting.autoCompileOnFocus &&
            ScriptEngine::get().hasSourceChanges())
        {
            (void)compileScriptsWithProgress(L"Hot Reload Scripts", true);
        }
    }

    if (isWindowsBuildRunning()) {
        m_windowsBuildProgress = std::min(0.92f, m_windowsBuildProgress + dt * 0.18f);
        setNativeStatus(m_windowsBuildStatus.empty() ? L"Building Windows package..." : m_windowsBuildStatus,
                        static_cast<int>(m_windowsBuildProgress * 100.0f));
    }
    updateWindowsBuildTask();

    if (m_statusClearTimer > 0.0f) {
        m_statusClearTimer -= dt;
        if (m_statusClearTimer <= 0.0f)
            clearNativeStatus();
    }

    m_processStatusTimer += dt;
    if (m_processStatusTimer >= 1.0f) {
        m_processStatusTimer = 0.0f;
        m_processManager.updateHealth();
        if (m_statusClearTimer <= 0.0f)
            setNativeStatus(m_processManager.statusLine());
    }
    updateWindowTitle();
}

void ImGuiEditorLayer::onRender(Renderer& renderer)
{
    auto scene = getActiveScene();
    if (!scene)
        return;

    Camera* camera = const_cast<Camera*>(&m_viewport.getCamera());
    if (m_playMode) {
        const EntityID cameraId = scene->getPrimaryCameraID();
        if (cameraId != NULL_ENTITY) {
            if (auto* sceneCamera = scene->getComponent<CameraComponent>(cameraId)) {
                if (const auto* transform = scene->getComponent<TransformComponent>(cameraId))
                    sceneCamera->camera.setFpsTransform(transform->translation, transform->rotation.y, transform->rotation.x);
                camera = &sceneCamera->camera;
            }
        }
    }

    const Entity selected = m_hierarchy.getSelectedEntity();
    scene->setSelectedEntity(m_playMode || !selected ? NULL_ENTITY : selected.getID());
    renderer.beginScene(*camera);
    scene->onRender(renderer);
    renderer.endScene();
}

void ImGuiEditorLayer::onGuiRender(Renderer& renderer, float, float)
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    renderEditorMenuBar();
    renderStatusBar();
    buildDockspace();

    ImGui::Render();
    ID3D12DescriptorHeap* heaps[] = {renderer.getSrvHeap().get()};
    renderer.getContext().getCommandList()->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), renderer.getContext().getCommandList());
}

void ImGuiEditorLayer::onEvent(Event& event)
{
    m_viewport.onEvent(event);
}

void ImGuiEditorLayer::setScene(std::shared_ptr<Scene> scene, bool seedIfEmpty)
{
    m_scene = std::move(scene);
    m_seedSceneOnReady = seedIfEmpty;
    m_hierarchy.setScene(m_scene);
    m_properties.setScene(m_scene.get());
    m_sceneDirty = false;
    m_paused = false;
    resetUndoHistory();
    updateWindowTitle();
}

void ImGuiEditorLayer::setProjectContext(std::string projectName,
                                         std::filesystem::path projectRoot,
                                         std::filesystem::path projectConfig)
{
    m_projectName = std::move(projectName);
    m_projectRoot = std::move(projectRoot);
    m_projectConfigPath = std::move(projectConfig);
    const auto assets = getAssetsRoot();
    m_contentBrowser.setRootPath(assets);
    m_properties.setAssetsRoot(assets);
    m_properties.setMaterialEditor(&m_materialEditor);
    m_contentBrowser.setMaterialEditor(&m_materialEditor);
    if (!m_projectRoot.empty() && !ProjectCache::ensureProjectDatabases(m_projectRoot))
        DEMON_LOG_WARN("ImGuiEditorLayer: failed to reserve project cache/index files in '{}'.",
                       m_projectRoot.string());
    (void)loadProjectSettings();
    ScriptEngine::get().configureProject(m_projectRoot, assets, m_projectSettings);
    updateWindowTitle();
}

void ImGuiEditorLayer::ensureSceneReady()
{
    if (!m_scene) {
        m_scene = Scene::create("Untitled Scene");
        m_hierarchy.setScene(m_scene);
        m_properties.setScene(m_scene.get());
    }

    if (m_seedSceneOnReady)
        seedDefaultScene();
    m_seedSceneOnReady = false;
}

void ImGuiEditorLayer::seedDefaultScene()
{
    addDefaultSceneEntities(m_scene);
}

void ImGuiEditorLayer::buildDockspace()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_None);

    if (!m_layoutBuilt) {
        m_layoutBuilt = true;
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
        ImGuiID center = dockspace;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Environment", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Content Browser", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Stats", bottom);
        ImGui::DockBuilderFinish(dockspace);
    }

    auto scene = getActiveScene();
    const Entity selectionBeforeHierarchy = m_hierarchy.getSelectedEntity();
    const EntityID selectionIdBeforeHierarchy = selectionBeforeHierarchy ? selectionBeforeHierarchy.getID() : NULL_ENTITY;
    if (m_showHierarchy)
        m_hierarchy.render();

    Entity selected = m_hierarchy.getSelectedEntity();
    const EntityID hierarchySelectionId = selected ? selected.getID() : NULL_ENTITY;
    const bool hierarchySelectionChanged = hierarchySelectionId != selectionIdBeforeHierarchy;
    if (hierarchySelectionChanged)
        DEMON_LOG_INFO("Editor selection changed by Hierarchy: {} -> {}",
                       selectionIdBeforeHierarchy, hierarchySelectionId);
    m_viewport.setSelectedEntity(selected);
    if (m_showViewport)
        m_viewport.render(scene);
    if (const auto requested = m_viewport.consumeSelectionRequest()) {
        DEMON_LOG_INFO("Editor selection requested by Viewport: {} (hierarchyChanged={})",
                       *requested, hierarchySelectionChanged);
        if (!hierarchySelectionChanged) {
            m_hierarchy.setSelectedEntity(scene ? scene->getEntityByID(*requested) : Entity{});
            selected = m_hierarchy.getSelectedEntity();
            m_viewport.setSelectedEntity(selected);
        }
    }

    if (m_showProperties)
        m_properties.render(selected);
    if (m_showContentBrowser)
        m_contentBrowser.render();
    if (m_showConsole)
        m_console.render();
    if (m_showStats)
        m_stats.render(Application::get().getRenderer().getStats());
    if (m_showEnvironment)
        m_environment.render(Application::get().getRenderer(), scene);
    if (m_showMaterialEditor)
        m_materialEditor.render();

    const bool hierarchyEdited = m_hierarchy.consumeSceneEdited();
    const bool propertiesEdited = m_properties.consumeSceneEdited();
    const bool viewportEdited = m_viewport.consumeSceneEdited();
    const bool environmentEdited = m_environment.consumeSceneEdited();
    const bool sceneEdited = hierarchyEdited || propertiesEdited || viewportEdited || environmentEdited;
    if (sceneEdited && pushUndoSnapshot()) {
        m_sceneDirty = true;
        updateWindowTitle();
    }
    if (m_environment.consumeGiApplyRequested())
        (void)applyGlobalIlluminationWithProgress(Application::get().getRenderer());
    if (m_environment.consumeShaderReloadRequested())
        (void)reloadShadersWithProgress();

    if (m_viewport.consumePlayToggleRequested()) {
        if (m_playMode)
            exitPlayMode();
        else
            (void)enterPlayMode();
    }

    renderProjectSettingsDialog();
    renderBuildConfigDialog();
    renderWindowsBuildProgressPopup();
}

void ImGuiEditorLayer::renderEditorMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    const bool hasScene = static_cast<bool>(m_scene);
    const bool hasSelection = static_cast<bool>(m_hierarchy.getSelectedEntity());
    const bool buildEnabled = !m_projectRoot.empty() && !isWindowsBuildRunning();

    auto commandItem = [this](UINT command,
                              const char* label,
                              const char* shortcut = nullptr,
                              bool selected = false,
                              bool enabled = true) {
        if (ImGui::MenuItem(label, shortcut, selected, enabled))
            handleNativeMenuCommand(command);
    };

    if (ImGui::BeginMenu("File")) {
        commandItem(MenuFileNewProject, "New Project...");
        commandItem(MenuFileOpenProject, "Open Project...");
        ImGui::Separator();
        commandItem(MenuFileNew, "New Scene", "Ctrl+N");
        commandItem(MenuFileOpen, "Open Scene...", "Ctrl+O");
        commandItem(MenuFileSave, "Save Scene", "Ctrl+S", false, hasScene);
        commandItem(MenuFileSaveAs, "Save Scene As...", "Ctrl+Shift+S", false, hasScene);
        ImGui::Separator();
        commandItem(MenuFileProjectSettings, "Project Settings...");
        commandItem(MenuFileBuildSettings, "Build Settings...");
        commandItem(MenuFileBuildWindows, "Build Windows Package...", nullptr, false, buildEnabled);
        commandItem(MenuFileDebugView, "Debug View", nullptr, m_viewport.getDebugModeEnabled());
        ImGui::Separator();
        commandItem(MenuFileExit, "Exit");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        commandItem(MenuEditUndo, "Undo", "Ctrl+Z", false, m_undoStack.size() > 1);
        commandItem(MenuEditRedo, "Redo", "Ctrl+Y", false, !m_redoStack.empty());
        ImGui::Separator();
        commandItem(MenuEditCreateEmpty, "Create Empty Entity", nullptr, false, !m_playMode);
        commandItem(MenuEditDuplicate, "Duplicate", "Ctrl+D", false, hasSelection && !m_playMode);
        commandItem(MenuEditDelete, "Delete", "Del", false, hasSelection && !m_playMode);
        commandItem(MenuEditSelectAll, "Select All", "Ctrl+A", false, !m_playMode);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        commandItem(MenuViewHierarchy, "Hierarchy", nullptr, m_showHierarchy);
        commandItem(MenuViewInspector, "Inspector", nullptr, m_showProperties);
        commandItem(MenuViewViewport, "Viewport", nullptr, m_showViewport);
        commandItem(MenuViewContentBrowser, "Content Browser", nullptr, m_showContentBrowser);
        commandItem(MenuViewConsole, "Console", nullptr, m_showConsole);
        commandItem(MenuViewStats, "Stats", nullptr, m_showStats);
        commandItem(MenuViewEnvironment, "Environment", nullptr, m_showEnvironment);
        commandItem(MenuViewMaterialEditor, "Material Editor", nullptr, m_showMaterialEditor);
        ImGui::Separator();
        commandItem(MenuViewLockDocking, "Lock Docking Layout", nullptr, EditorSettings::dockingLocked);
        commandItem(MenuViewResetLayout, "Reset ImGui Layout");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        commandItem(MenuToolsCompileShaders, "Compile Shaders");
        commandItem(MenuToolsHotReloadShaders, "Hot Reload Shaders", "Ctrl+R");
        commandItem(MenuToolsCompileScripts, "Compile Scripts");
        commandItem(MenuToolsHotReloadScripts, "Hot Reload Scripts");
        ImGui::Separator();
        commandItem(MenuToolsOpenProjectFolder, "Open Project Folder", nullptr, false, !m_projectRoot.empty());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play")) {
        commandItem(MenuPlayToggle, m_playMode ? "Stop" : "Play", "F5", m_playMode);
        commandItem(MenuPlayPause, m_paused ? "Resume" : "Pause", "F6", m_paused, m_playMode);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        commandItem(MenuHelpAbout, "About DemonEngine");
        ImGui::EndMenu();
    }

    const std::string mode = m_playMode ? (m_paused ? "Runtime Paused" : "Runtime Live") : "Scene Editing";
    const float modeWidth = ImGui::CalcTextSize(mode.c_str()).x;
    const float cursorX = ImGui::GetCursorPosX();
    const float targetX = ImGui::GetWindowWidth() - modeWidth - ImGui::GetStyle().FramePadding.x * 2.0f;
    if (targetX > cursorX)
        ImGui::SetCursorPosX(targetX);
    ImGui::TextDisabled("%s", mode.c_str());

    ImGui::EndMainMenuBar();
}

void ImGuiEditorLayer::renderStatusBar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport)
        return;

    constexpr float kStatusHeight = 28.0f;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoDecoration;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.72f, 0.12f, 0.35f));

    if (ImGui::BeginViewportSideBar("##DemonEditorStatusBar", viewport, ImGuiDir_Down, kStatusHeight, flags)) {
        const bool showProgress = isWindowsBuildRunning() ||
                                  (m_statusProgressPercent >= 0 && m_statusClearTimer > 0.0f);
        const float progressWidth = showProgress ? 150.0f : 0.0f;
        const std::string rightText = std::format("{}  |  {}",
                                                  m_projectName.empty() ? "Untitled Project" : m_projectName,
                                                  m_playMode ? (m_paused ? "Paused" : "Runtime") : "Editor");
        const float rightWidth = ImGui::CalcTextSize(rightText.c_str()).x;
        const float availableWidth = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f;
        const float statusMaxWidth = std::max(80.0f, availableWidth - rightWidth - progressWidth - 28.0f);

        std::string status = m_statusText.empty() ? "Ready" : m_statusText;
        const bool needsEllipsis = ImGui::CalcTextSize(status.c_str()).x > statusMaxWidth;
        if (needsEllipsis) {
            while (status.size() > 4 &&
                   ImGui::CalcTextSize((status + "...").c_str()).x > statusMaxWidth)
            {
                status.resize(status.size() - 1);
            }
            status += "...";
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(status.c_str());

        const float rightEdge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        if (showProgress) {
            const float progressX = rightEdge - rightWidth - progressWidth - 14.0f;
            if (progressX > ImGui::GetCursorPosX()) {
                ImGui::SameLine();
                ImGui::SetCursorPosX(progressX);
                const float progress = std::clamp(static_cast<float>(m_statusProgressPercent) / 100.0f,
                                                  0.0f,
                                                  1.0f);
                ImGui::ProgressBar(progress, ImVec2(progressWidth, 0.0f), "");
            }
        }

        const float rightX = rightEdge - rightWidth;
        if (rightX > ImGui::GetCursorPosX()) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(rightX);
            ImGui::TextDisabled("%s", rightText.c_str());
        }

        ImGui::End();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void ImGuiEditorLayer::setNativeStatus(std::wstring text, int progressPercent, float holdSeconds)
{
    m_statusText = wideToUtf8(text);
    if (m_statusText.empty())
        m_statusText = "Ready";
    m_statusProgressPercent = progressPercent;
    m_statusClearTimer = holdSeconds;
}

void ImGuiEditorLayer::clearNativeStatus()
{
    m_statusText = "Ready";
    m_statusProgressPercent = -1;
    m_statusClearTimer = 0.0f;
}

void ImGuiEditorLayer::handleNativeMenuCommand(UINT command)
{
    switch (command) {
        case MenuFileNewProject: (void)newProjectDialog(); break;
        case MenuFileOpenProject: (void)openProjectDialog(); break;
        case MenuFileNew: newScene(); break;
        case MenuFileOpen: (void)openSceneDialog(); break;
        case MenuFileSave: (void)saveScene(); break;
        case MenuFileSaveAs: (void)saveSceneAs(); break;
        case MenuFileProjectSettings: openProjectSettingsDialog(); break;
        case MenuFileBuildSettings: openBuildConfigDialog(); break;
        case MenuFileBuildWindows: (void)buildWindowsPackageWithProgress(); break;
        case MenuFileDebugView: m_viewport.setDebugModeEnabled(!m_viewport.getDebugModeEnabled()); break;
        case MenuFileExit: Application::get().close(); break;
        case MenuEditUndo: undoScene(); break;
        case MenuEditRedo: redoScene(); break;
        case MenuEditCreateEmpty:
            if (m_scene) {
                Entity entity = m_scene->createEntity("Entity");
                entity.addComponent<TransformComponent>();
                m_hierarchy.setSelectedEntity(entity);
                m_sceneDirty = true;
                pushUndoSnapshot();
            }
            break;
        case MenuEditDuplicate: duplicateSelectedEntity(); break;
        case MenuEditDelete: deleteSelectedEntity(); break;
        case MenuEditSelectAll: m_hierarchy.selectAllEntities(); break;
        case MenuViewHierarchy: m_showHierarchy = !m_showHierarchy; break;
        case MenuViewInspector: m_showProperties = !m_showProperties; break;
        case MenuViewViewport: m_showViewport = !m_showViewport; break;
        case MenuViewContentBrowser: m_showContentBrowser = !m_showContentBrowser; break;
        case MenuViewConsole: m_showConsole = !m_showConsole; break;
        case MenuViewStats: m_showStats = !m_showStats; break;
        case MenuViewEnvironment: m_showEnvironment = !m_showEnvironment; break;
        case MenuViewMaterialEditor: m_showMaterialEditor = !m_showMaterialEditor; break;
        case MenuViewLockDocking: EditorSettings::dockingLocked = !EditorSettings::dockingLocked; break;
        case MenuViewResetLayout:
            ImGui::LoadIniSettingsFromMemory("");
            m_layoutBuilt = false;
            break;
        case MenuToolsCompileShaders: (void)compileShadersWithProgress(); break;
        case MenuToolsHotReloadShaders: (void)reloadShadersWithProgress(); break;
        case MenuToolsCompileScripts: (void)compileScriptsWithProgress(L"Compile Scripts", false); break;
        case MenuToolsHotReloadScripts: (void)compileScriptsWithProgress(L"Hot Reload Scripts", true); break;
        case MenuToolsOpenProjectFolder:
            if (!m_projectRoot.empty())
                ShellExecuteW(nullptr, L"open", m_projectRoot.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case MenuPlayToggle:
            if (m_playMode) exitPlayMode(); else (void)enterPlayMode();
            break;
        case MenuPlayPause:
            if (m_playMode) m_paused = !m_paused;
            break;
        case MenuHelpAbout:
            MessageBoxW(Application::get().getWindow().getWin32Handle(),
                        L"DemonEngine Editor v1.2\nCODE STUDIO GAMES\nWin32 + Dear ImGui + DirectX 12",
                        L"About DemonEngine", MB_OK | MB_ICONINFORMATION);
            break;
        default: break;
    }
    updateWindowTitle();
}

void ImGuiEditorLayer::resetUndoHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    if (m_scene)
        m_undoStack.push_back(SceneSerializer(m_scene).serializeToString());
}

bool ImGuiEditorLayer::pushUndoSnapshot()
{
    if (!m_scene || m_playMode)
        return false;
    std::string snapshot = SceneSerializer(m_scene).serializeToString();
    if (!m_undoStack.empty() && m_undoStack.back() == snapshot)
        return false;
    m_undoStack.push_back(std::move(snapshot));
    if (m_undoStack.size() > 128)
        m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
    return true;
}

bool ImGuiEditorLayer::restoreSceneSnapshot(const std::string& snapshot, bool markDirty)
{
    auto restored = Scene::create(getSceneDisplayName());
    if (!SceneSerializer(restored).deserializeFromString(snapshot))
        return false;
    m_scene = std::move(restored);
    m_hierarchy.setScene(m_scene);
    m_properties.setScene(m_scene.get());
    m_viewport.setSelectedEntity({});
    m_sceneDirty = markDirty;
    updateWindowTitle();
    return true;
}

void ImGuiEditorLayer::undoScene()
{
    if (m_playMode || m_undoStack.size() <= 1)
        return;
    m_redoStack.push_back(std::move(m_undoStack.back()));
    m_undoStack.pop_back();
    (void)restoreSceneSnapshot(m_undoStack.back(), true);
}

void ImGuiEditorLayer::redoScene()
{
    if (m_playMode || m_redoStack.empty())
        return;
    std::string snapshot = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    if (restoreSceneSnapshot(snapshot, true))
        m_undoStack.push_back(std::move(snapshot));
}

void ImGuiEditorLayer::syncProjectSettingsBuffersFromDraft()
{
    auto copy = [](auto& destination, const std::string& value) {
        std::snprintf(destination.data(), destination.size(), "%s", value.c_str());
    };
    copy(m_buildAppNameBuf, m_projectSettingsDraft.build.appName);
    copy(m_buildCompanyBuf, m_projectSettingsDraft.build.companyName);
    copy(m_buildVersionBuf, m_projectSettingsDraft.build.appVersion);
    copy(m_buildOutputBuf, m_projectSettingsDraft.build.outputName);
    copy(m_buildStartupSceneBuf, m_projectSettingsDraft.build.startupScene);
    copy(m_buildBundleIdBuf, m_projectSettingsDraft.build.bundleId);
    copy(m_buildDirectoryBuf, m_projectSettingsDraft.build.buildDirectory);
    copy(m_scriptingIdePathBuf, m_projectSettingsDraft.scripting.idePath.string());
}

void ImGuiEditorLayer::syncProjectSettingsDraftFromBuffers()
{
    m_projectSettingsDraft.build.appName = m_buildAppNameBuf.data();
    m_projectSettingsDraft.build.companyName = m_buildCompanyBuf.data();
    m_projectSettingsDraft.build.appVersion = m_buildVersionBuf.data();
    m_projectSettingsDraft.build.outputName = m_buildOutputBuf.data();
    m_projectSettingsDraft.build.startupScene = m_buildStartupSceneBuf.data();
    m_projectSettingsDraft.build.bundleId = m_buildBundleIdBuf.data();
    m_projectSettingsDraft.build.buildDirectory = m_buildDirectoryBuf.data();
    m_projectSettingsDraft.scripting.idePath = m_scriptingIdePathBuf.data();
}

bool ImGuiEditorLayer::loadProjectSettings()
{
    m_projectSettings.applyProjectDefaults(m_projectName);
    if (m_projectConfigPath.empty())
        return true;
    return m_projectSettings.load(m_projectConfigPath, m_projectName);
}

bool ImGuiEditorLayer::saveProjectSettings()
{
    if (m_projectConfigPath.empty())
        return false;
    const bool saved = m_projectSettings.save(m_projectConfigPath);
    if (saved) {
        ScriptEngine::get().configureProject(m_projectRoot, getAssetsRoot(), m_projectSettings);
        setNativeStatus(L"Project settings saved", 100, 2.0f);
    }
    return saved;
}

void ImGuiEditorLayer::openProjectSettingsDialog()
{
    m_projectSettingsDraft = m_projectSettings;
    syncProjectSettingsBuffersFromDraft();
    m_showProjectSettings = true;
}

void ImGuiEditorLayer::openBuildConfigDialog()
{
    m_projectSettingsDraft = m_projectSettings;
    syncProjectSettingsBuffersFromDraft();
    m_showBuildConfig = true;
}

void ImGuiEditorLayer::renderProjectSettingsDialog()
{
    if (!m_showProjectSettings)
        return;
    ImGui::SetNextWindowSize({560.0f, 430.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Project Settings", &m_showProjectSettings, ImGuiWindowFlags_NoDocking)) {
        ImGui::SeparatorText("Application");
        ImGui::InputText("Application Name", m_buildAppNameBuf.data(), m_buildAppNameBuf.size());
        ImGui::InputText("Company", m_buildCompanyBuf.data(), m_buildCompanyBuf.size());
        ImGui::InputText("Version", m_buildVersionBuf.data(), m_buildVersionBuf.size());
        ImGui::InputText("Bundle ID", m_buildBundleIdBuf.data(), m_buildBundleIdBuf.size());
        ImGui::SeparatorText("Scripting");
        ImGui::InputText("IDE Path", m_scriptingIdePathBuf.data(), m_scriptingIdePathBuf.size());
        ImGui::Checkbox("Auto compile on focus", &m_projectSettingsDraft.scripting.autoCompileOnFocus);
        ImGui::Checkbox("Hot reload enabled", &m_projectSettingsDraft.scripting.hotReloadEnabled);
        ImGui::Separator();
        if (ImGui::Button("Save", {110.0f, 0.0f})) {
            syncProjectSettingsDraftFromBuffers();
            m_projectSettings = m_projectSettingsDraft;
            if (saveProjectSettings())
                m_showProjectSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0f, 0.0f}))
            m_showProjectSettings = false;
    }
    ImGui::End();
}

void ImGuiEditorLayer::renderBuildConfigDialog()
{
    if (!m_showBuildConfig)
        return;
    ImGui::SetNextWindowSize({590.0f, 480.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Windows Build Settings", &m_showBuildConfig, ImGuiWindowFlags_NoDocking)) {
        ImGui::InputText("App Name", m_buildAppNameBuf.data(), m_buildAppNameBuf.size());
        ImGui::InputText("Company", m_buildCompanyBuf.data(), m_buildCompanyBuf.size());
        ImGui::InputText("Version", m_buildVersionBuf.data(), m_buildVersionBuf.size());
        ImGui::InputText("Output Name", m_buildOutputBuf.data(), m_buildOutputBuf.size());
        ImGui::InputText("Build Directory", m_buildDirectoryBuf.data(), m_buildDirectoryBuf.size());
        ImGui::InputText("Startup Scene", m_buildStartupSceneBuf.data(), m_buildStartupSceneBuf.size());
        ImGui::Checkbox("Compile shaders", &m_projectSettingsDraft.build.compileShadersOnBuild);
        ImGui::Checkbox("Package assets", &m_projectSettingsDraft.build.packageAssets);
        ImGui::Checkbox("Package shaders", &m_projectSettingsDraft.build.packageShaders);
        ImGui::Checkbox("Copy player executable and runtime", &m_projectSettingsDraft.build.copyExecutable);
        ImGui::Separator();
        if (ImGui::Button("Save Settings", {130.0f, 0.0f})) {
            syncProjectSettingsDraftFromBuffers();
            m_projectSettings = m_projectSettingsDraft;
            (void)saveProjectSettings();
        }
        ImGui::SameLine();
        const bool buildRunning = isWindowsBuildRunning();
        if (buildRunning)
            ImGui::BeginDisabled();
        if (ImGui::Button("Build Windows", {130.0f, 0.0f})) {
            syncProjectSettingsDraftFromBuffers();
            m_projectSettings = m_projectSettingsDraft;
            (void)saveProjectSettings();
            if (buildWindowsPackageWithProgress())
                m_showBuildConfig = false;
        }
        if (buildRunning)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", {90.0f, 0.0f}))
            m_showBuildConfig = false;
    }
    ImGui::End();
}

bool ImGuiEditorLayer::isWindowsBuildRunning() const
{
    return m_windowsBuildFuture.valid() && !m_windowsBuildCompleted;
}

void ImGuiEditorLayer::updateWindowsBuildTask()
{
    if (!m_windowsBuildFuture.valid() || m_windowsBuildCompleted)
        return;

    if (m_windowsBuildFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    WindowsBuildTaskResult task;
    try {
        task = m_windowsBuildFuture.get();
    } catch (const std::exception& ex) {
        task.success = false;
        task.message = std::string("The Windows package build crashed: ") + ex.what();
    } catch (...) {
        task.success = false;
        task.message = "The Windows package build crashed with an unknown error.";
    }

    m_windowsBuildCompleted = true;
    m_windowsBuildSuccess = task.success;
    m_windowsBuildProgress = task.success ? 1.0f : 0.0f;
    m_lastWindowsBuildRoot = task.buildRoot;
    m_lastWindowsBuildExecutable = task.executablePath;
    m_windowsBuildMessage = std::move(task.message);
    m_windowsBuildStatus = task.success ? L"Windows package complete" : L"Windows package failed";
    setNativeStatus(m_windowsBuildStatus, task.success ? 100 : 0, 4.0f);
}

void ImGuiEditorLayer::renderWindowsBuildProgressPopup()
{
    if (!m_windowsBuildPopupOpen)
        return;

    updateWindowsBuildTask();

    ImGui::OpenPopup("Windows Package Build");
    ImGui::SetNextWindowSize({460.0f, 0.0f}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Windows Package Build",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        const char* statusText = "Building Windows package...";
        if (m_windowsBuildCompleted)
            statusText = m_windowsBuildSuccess ? "Windows package complete" : "Windows package failed";
        ImGui::TextUnformatted(statusText);
        ImGui::ProgressBar(std::clamp(m_windowsBuildProgress, 0.0f, 1.0f), {420.0f, 0.0f});

        if (isWindowsBuildRunning()) {
            ImGui::TextUnformatted("Packaging scene, assets, shaders, and runtime files...");
        } else if (m_windowsBuildCompleted) {
            ImGui::Separator();
            ImVec4 color = m_windowsBuildSuccess
                ? ImVec4(0.30f, 0.85f, 0.45f, 1.0f)
                : ImVec4(0.95f, 0.35f, 0.32f, 1.0f);
            ImGui::TextColored(color, "%s", m_windowsBuildSuccess ? "Build complete" : "Build failed");
            if (!m_windowsBuildMessage.empty())
                ImGui::TextWrapped("%s", m_windowsBuildMessage.c_str());

            if (m_windowsBuildSuccess && !m_lastWindowsBuildExecutable.empty()) {
                if (ImGui::Button("Launch", {110.0f, 0.0f})) {
                    const std::wstring executablePath = m_lastWindowsBuildExecutable.wstring();
                    const std::wstring workingDirectory = m_lastWindowsBuildRoot.wstring();
                    ShellExecuteW(nullptr,
                                  L"open",
                                  executablePath.c_str(),
                                  nullptr,
                                  workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                  SW_SHOWNORMAL);
                }
                ImGui::SameLine();
                if (ImGui::Button("Open Folder", {110.0f, 0.0f})) {
                    const std::wstring buildRoot = m_lastWindowsBuildRoot.wstring();
                    ShellExecuteW(nullptr,
                                  L"open",
                                  buildRoot.c_str(),
                                  nullptr,
                                  nullptr,
                                  SW_SHOWNORMAL);
                }
                ImGui::SameLine();
            }

            if (ImGui::Button("Close", {90.0f, 0.0f})) {
                m_windowsBuildPopupOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

bool ImGuiEditorLayer::compileShadersWithProgress()
{
    if (m_projectRoot.empty())
        return false;
    const std::filesystem::path executable = executablePath();
    const std::filesystem::path compiler = executable.parent_path() / "DemonShaderCompiler.exe";
    std::filesystem::path shaderSource = getAssetsRoot() / "shaders" / "hlsl";
    if (!std::filesystem::exists(shaderSource))
        shaderSource = std::filesystem::current_path() / "assets" / "shaders" / "hlsl";
    if (!std::filesystem::exists(shaderSource))
        shaderSource = executable.parent_path() / "assets" / "shaders" / "hlsl";

    setNativeStatus(L"Compiling shaders...", 10);
    const bool success = ProjectCache::compileShaders(m_projectRoot, shaderSource, compiler);
    setNativeStatus(success ? L"Shader compilation complete" : L"Shader compilation failed",
                    success ? 100 : 0, 3.0f);
    if (!success)
        MessageBoxW(Application::get().getWindow().getWin32Handle(),
                    L"Shader compilation failed. Check the engine terminal for DXC diagnostics.",
                    L"Compile Shaders", MB_OK | MB_ICONERROR);
    return success;
}

bool ImGuiEditorLayer::reloadShadersWithProgress()
{
    const std::filesystem::path executable = executablePath();
    const std::filesystem::path compiler = executable.parent_path() / "DemonShaderCompiler.exe";

    // The live renderer loads .cso files relative to its working directory
    // (assets/shaders/dx12). Resolve the matching source/output pair, trying the
    // working directory first so the reload actually hits the running pipelines.
    std::filesystem::path shaderSource = std::filesystem::current_path() / "assets" / "shaders" / "hlsl";
    std::filesystem::path outputDir    = std::filesystem::current_path() / "assets" / "shaders" / "dx12";
    if (!std::filesystem::exists(shaderSource)) {
        shaderSource = getAssetsRoot() / "shaders" / "hlsl";
        outputDir    = getAssetsRoot() / "shaders" / "dx12";
    }
    if (!std::filesystem::exists(shaderSource)) {
        shaderSource = executable.parent_path() / "assets" / "shaders" / "hlsl";
        outputDir    = executable.parent_path() / "assets" / "shaders" / "dx12";
    }

    HWND parent = Application::get().getWindow().getWin32Handle();
    ShaderReloadPopup popup = createShaderReloadPopup(parent);
    EnableWindow(parent, FALSE);
    setNativeStatus(L"Hot reloading shaders...", 10);
    updateShaderReloadPopup(popup, L"Compiling HLSL shaders with DXC...", 8);

    // Compile asynchronously so the progress bar keeps animating while DXC runs.
    std::future<bool> compileTask = std::async(std::launch::async,
        [compiler, shaderSource, outputDir]() {
            return runShaderCompilerProcess(compiler, shaderSource, outputDir);
        });

    int progress = 8;
    while (compileTask.wait_for(std::chrono::milliseconds(40)) != std::future_status::ready) {
        progress = std::min(progress + 1, 68);
        updateShaderReloadPopup(popup, L"Compiling HLSL shaders with DXC...", progress);
    }
    const bool compiled = compileTask.get();

    bool reloaded = false;
    if (compiled) {
        updateShaderReloadPopup(popup, L"Rebuilding GPU pipeline state objects...", 82);
        reloaded = Application::get().getRenderer().reloadPipelines();
        Application::get().getRenderer().getPostProcessing().invalidateHistory();
        updateShaderReloadPopup(popup,
            reloaded ? L"Shader hot reload complete." : L"Compiled, but pipeline rebuild failed.",
            100);
    } else {
        updateShaderReloadPopup(popup, L"Shader compilation failed - see engine terminal.", 100);
    }

    // Hold the completed bar briefly so the result is legible.
    for (int i = 0; i < 14; ++i) {
        pumpUiMessages();
        Sleep(30);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    destroyShaderReloadPopup(popup);

    const bool success = compiled && reloaded;
    setNativeStatus(success ? L"Shaders hot reloaded" : L"Shader hot reload failed",
                    success ? 100 : 0, 3.0f);
    if (!compiled)
        MessageBoxW(parent,
                    L"Shader compilation failed. Check the engine terminal for DXC diagnostics.",
                    L"Shader Hot Reload", MB_OK | MB_ICONERROR);
    return success;
}

bool ImGuiEditorLayer::compileScriptsWithProgress(const wchar_t* title, bool hotReloadOnly)
{
    ScriptEngine& scripts = ScriptEngine::get();
    scripts.configureProject(m_projectRoot, getAssetsRoot(), m_projectSettings);
    setNativeStatus(title ? title : L"Compiling scripts...", 10);
    bool success = false;
    if (hotReloadOnly) {
        success = scripts.compileAndHotReload(m_scene.get(), m_runtimeScene.get());
    } else {
        const ScriptCompileResult result = scripts.rebuildRegistry();
        success = result.success;
        if (success) {
            if (m_scene) scripts.refreshScene(*m_scene);
            if (m_runtimeScene) scripts.refreshScene(*m_runtimeScene);
        }
    }
    setNativeStatus(success ? L"Script compilation complete" : L"Script compilation failed",
                    success ? 100 : 0, 3.0f);
    return success;
}

bool ImGuiEditorLayer::buildWindowsPackageWithProgress()
{
    if (m_projectRoot.empty())
        return false;
    if (isWindowsBuildRunning()) {
        m_windowsBuildPopupOpen = true;
        return false;
    }

    if (m_scenePath.empty()) {
        if (!saveSceneAs())
            return false;
    } else if (m_sceneDirty && !saveScene()) {
        return false;
    }

    const std::filesystem::path executable = executablePath();
    const std::filesystem::path gamePlayer = executable.parent_path() / "DemonGamePlayer.exe";
    if (!std::filesystem::exists(gamePlayer)) {
        MessageBoxW(Application::get().getWindow().getWin32Handle(),
                    L"DemonGamePlayer.exe was not found next to the editor. Build the DemonGamePlayer target before exporting.",
                    L"Build Windows Package", MB_OK | MB_ICONERROR);
        return false;
    }

    std::filesystem::path shaderSource = getAssetsRoot() / "shaders" / "hlsl";
    if (!std::filesystem::exists(shaderSource))
        shaderSource = std::filesystem::current_path() / "assets" / "shaders" / "hlsl";
    const std::filesystem::path compiler = executable.parent_path() / "DemonShaderCompiler.exe";
    ProjectSettings::BuildConfig buildConfig = m_projectSettings.build;
    if (buildConfig.appName.empty())
        buildConfig.appName = m_projectName.empty() ? "DemonGame" : m_projectName;
    if (buildConfig.outputName.empty())
        buildConfig.outputName = buildConfig.appName;
    if (buildConfig.appVersion.empty())
        buildConfig.appVersion = "1.0.0";
    if (buildConfig.startupScene.empty() && !m_scenePath.empty())
        buildConfig.startupScene = m_scenePath.string();

    const std::filesystem::path projectRoot = m_projectRoot;
    const std::filesystem::path assetsRoot = getAssetsRoot();
    m_windowsBuildPopupOpen = true;
    m_windowsBuildCompleted = false;
    m_windowsBuildSuccess = false;
    m_windowsBuildProgress = 0.08f;
    m_windowsBuildMessage.clear();
    m_lastWindowsBuildRoot.clear();
    m_lastWindowsBuildExecutable.clear();
    m_windowsBuildStatus = L"Building Windows package...";
    setNativeStatus(m_windowsBuildStatus, 10);

    m_windowsBuildFuture = std::async(std::launch::async,
        [projectRoot, assetsRoot, shaderSource, compiler, gamePlayer, buildConfig]() mutable {
            WindowsBuildTaskResult task;
            ProjectCache::BuildPackageResult result;
            try {
                task.success = ProjectCache::buildWindowsPackage(
                    projectRoot, assetsRoot, shaderSource, compiler, gamePlayer, buildConfig, &result);
                task.buildRoot = result.buildRoot;
                task.executablePath = result.executablePath;
                if (task.success) {
                    task.message = "Build completed:\n" + result.buildRoot.string();
                    if (!result.startupScenePath.empty())
                        task.message += "\nStartup scene:\n" + result.startupScenePath.string();
                } else {
                    task.message = "The Windows package build failed. Check the engine terminal for details.";
                }
            } catch (const std::exception& ex) {
                task.success = false;
                task.message = std::string("The Windows package build crashed: ") + ex.what();
            } catch (...) {
                task.success = false;
                task.message = "The Windows package build crashed with an unknown error.";
            }
            return task;
        });
    return true;
}

bool ImGuiEditorLayer::newProjectDialog()
{
    if (!confirmSceneChange("switch projects"))
        return false;
    const auto project = ProjectHub::show();
    if (!project)
        return false;
    setProjectContext(project->name, project->projectDir, project->configPath);
    setScene(Scene::create("Untitled Scene"), true);
    m_scenePath.clear();
    return true;
}

bool ImGuiEditorLayer::openProjectDialog()
{
    if (!confirmSceneChange("open another project"))
        return false;
    const auto config = FileDialogs::openFile(
        "Demon Project (*.demonproj)\0*.demonproj\0Legacy Project (*.json)\0*.json\0All Files (*.*)\0*.*\0");
    if (!config)
        return false;
    const std::filesystem::path path = *config;
    setProjectContext(path.stem().string(), path.parent_path(), path);
    setScene(Scene::create("Untitled Scene"), true);
    m_scenePath.clear();
    return true;
}

void ImGuiEditorLayer::updateWindowTitle() const
{
    const std::string sceneName = getSceneDisplayName();
    const std::string dirtyMarker = m_sceneDirty ? "*" : "";
    const std::string mode = m_playMode ? (m_paused ? " [PAUSED]" : " [PLAY]") : "";
    const std::string title = std::format("{}{} - {}{} - DemonEngine Editor v1.2",
                                          sceneName, dirtyMarker, m_projectName, mode);
    SetWindowTextA(Application::get().getWindow().getWin32Handle(), title.c_str());
}

void ImGuiEditorLayer::syncSceneDirtyState()
{
    updateWindowTitle();
}

void ImGuiEditorLayer::syncScriptHotReload()
{
    if (m_projectSettings.scripting.hotReloadEnabled && ScriptEngine::get().hasSourceChanges())
        (void)compileScriptsWithProgress(L"Hot Reload Scripts", true);
}

bool ImGuiEditorLayer::applyGlobalIlluminationWithProgress(Renderer& renderer)
{
    // Turn the feature on immediately so the effect is visible even when no
    // project is loaded.
    renderer.settings().gi.enabled = true;

    const auto mode = renderer.settings().gi.mode == GlobalIlluminationSettings::Mode::Realtime
        ? "realtime" : "baked";

    // "Apply GI" recompiles the lighting shaders (which host the GI bounce code)
    // and hot-reloads the pipelines through the shared Win32 progress popup, so
    // the new indirect lighting shows up right away instead of only writing a
    // bake manifest to disk.
    const bool reloaded = reloadShadersWithProgress();

    bool manifest = true;
    if (!m_projectRoot.empty())
        manifest = ProjectCache::writeGlobalIlluminationBakeManifest(m_projectRoot, mode, m_projectName);

    const bool success = reloaded && manifest;
    setNativeStatus(success ? L"Global illumination applied" : L"Global illumination failed",
                    success ? 100 : 0, 3.0f);
    return success;
}

bool ImGuiEditorLayer::prepareRuntimeArtifacts(const std::shared_ptr<Scene>& scene)
{
    if (!scene || m_projectRoot.empty())
        return false;
    return ProjectCache::ensureLayout(m_projectRoot) &&
           ProjectCache::ensureProjectDatabases(m_projectRoot) &&
           ProjectCache::writeSceneAssetManifest(m_projectRoot, *scene) &&
           ProjectCache::writeCacheManifest(m_projectRoot, m_projectName) &&
           compileScriptsWithProgress(L"Preparing runtime scripts", true);
}

void ImGuiEditorLayer::newScene()
{
    if (!confirmSceneChange("create a new scene"))
        return;
    setScene(Scene::create("Untitled Scene"), true);
    m_scenePath.clear();
    m_sceneDirty = false;
    updateWindowTitle();
}

bool ImGuiEditorLayer::openSceneDialog()
{
    const auto path = FileDialogs::openFile(
        "Demon Scene (*.demonscene)\0*.demonscene\0All Files (*.*)\0*.*\0",
        getAssetsRoot().string().c_str());
    return path && openSceneFromPath(*path);
}

bool ImGuiEditorLayer::openSceneFromPath(const std::filesystem::path& path)
{
    if (!isDemonSceneFile(path)) {
        showInvalidDemonSceneFileError();
        return false;
    }

    if (!confirmSceneChange("open another scene"))
        return false;
    auto scene = Scene::create(path.stem().string());
    SceneSerializer serializer(scene);
    if (!serializer.deserialize(path.string())) {
        showInvalidDemonSceneFileError();
        return false;
    }
    setScene(std::move(scene), false);
    m_scenePath = path;
    updateWindowTitle();
    return true;
}

bool ImGuiEditorLayer::saveScene()
{
    return m_scenePath.empty() ? saveSceneAs() : saveSceneToPath(m_scenePath);
}

bool ImGuiEditorLayer::saveSceneAs()
{
    const auto path = FileDialogs::saveFile(
        "Demon Scene (*.demonscene)\0*.demonscene\0",
        "demonscene", getAssetsRoot().string().c_str());
    return path && saveSceneToPath(*path);
}

bool ImGuiEditorLayer::saveSceneToPath(const std::filesystem::path& path)
{
    if (!m_scene)
        return false;
    std::filesystem::path scenePath = path;
    if (!isDemonSceneFile(scenePath))
        scenePath.replace_extension(".demonscene");

    SceneSerializer serializer(m_scene);
    if (!serializer.serialize(scenePath.string()))
        return false;
    m_scenePath = scenePath;
    m_sceneDirty = false;
    setNativeStatus(L"Scene saved", 100, 2.0f);
    updateWindowTitle();
    return true;
}

bool ImGuiEditorLayer::confirmSceneChange(const char* actionLabel)
{
    if (!m_sceneDirty)
        return true;
    const std::string prompt = "Save the current scene before you " + std::string(actionLabel) + "?";
    const int result = MessageBoxA(Application::get().getWindow().getWin32Handle(), prompt.c_str(),
                                   "Unsaved Scene", MB_ICONWARNING | MB_YESNOCANCEL);
    if (result == IDCANCEL)
        return false;
    return result != IDYES || saveScene();
}

void ImGuiEditorLayer::duplicateSelectedEntity()
{
    if (!m_scene)
        return;
    const Entity selected = m_hierarchy.getSelectedEntity();
    if (!selected)
        return;
    m_hierarchy.setSelectedEntity(m_scene->getEntityByID(m_scene->duplicateEntity(selected.getID()).getID()));
    m_sceneDirty = true;
    pushUndoSnapshot();
    updateWindowTitle();
}

void ImGuiEditorLayer::deleteSelectedEntity()
{
    if (!m_scene)
        return;
    const Entity selected = m_hierarchy.getSelectedEntity();
    if (!selected)
        return;
    m_scene->destroyEntity(selected.getID());
    m_hierarchy.setSelectedEntity({});
    m_sceneDirty = true;
    pushUndoSnapshot();
    updateWindowTitle();
}

bool ImGuiEditorLayer::enterPlayMode()
{
    if (!m_scene)
        return false;
    if (!prepareRuntimeArtifacts(m_scene))
        DEMON_LOG_WARN("ImGuiEditorLayer: entering play mode with incomplete runtime artifacts.");
    m_runtimeScene = Scene::copy(m_scene);
    m_runtimeScene->setSimulationEnabled(true);
    m_playMode = true;
    m_paused = false;
    m_viewport.setPlayMode(true);
    updateWindowTitle();
    return true;
}

void ImGuiEditorLayer::exitPlayMode()
{
    m_playMode = false;
    m_paused = false;
    m_viewport.setPlayMode(false);
    m_runtimeScene.reset();
    updateWindowTitle();
}

std::shared_ptr<Scene> ImGuiEditorLayer::getActiveScene() const
{
    return m_playMode && m_runtimeScene ? m_runtimeScene : m_scene;
}

std::string ImGuiEditorLayer::getSceneDisplayName() const
{
    if (!m_scenePath.empty())
        return m_scenePath.stem().string();
    return m_scene && !m_scene->getName().empty() ? m_scene->getName() : "Untitled Scene";
}

std::filesystem::path ImGuiEditorLayer::getAssetsRoot() const
{
    return m_projectRoot.empty() ? std::filesystem::path("assets") : m_projectRoot / "assets";
}

} // namespace Demon
