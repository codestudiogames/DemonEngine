#pragma once

#include "core/Layer.h"
#include "scene/Scene.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/ViewportPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/StatsPanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/MaterialEditorPanel.h"
#include "core/ProjectSettings.h"
#include "ProcessManager.h"

#include <future>

namespace Demon {

class ImGuiEditorLayer : public Layer {
public:
    ImGuiEditorLayer();
    ~ImGuiEditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(float dt) override;
    void onRender(Renderer& renderer) override;
    void onGuiRender(Renderer& renderer, float displayW, float displayH) override;
    void onEvent(Event& e) override;

    void setScene(std::shared_ptr<Scene> scene, bool seedIfEmpty = true);
    void setProjectContext(std::string projectName,
                           std::filesystem::path projectRoot,
                           std::filesystem::path projectConfig = {});

private:
    void ensureSceneReady();
    void buildDockspace();
    void seedDefaultScene();
    void renderEditorMenuBar();
    void renderStatusBar();
    void setNativeStatus(std::wstring text, int progressPercent = -1, float holdSeconds = 0.0f);
    void clearNativeStatus();
    void handleNativeMenuCommand(UINT commandId);
    void syncSceneDirtyState();
    void resetUndoHistory();
    bool pushUndoSnapshot();
    bool restoreSceneSnapshot(const std::string& snapshot, bool markDirty);
    void undoScene();
    void redoScene();
    void openProjectSettingsDialog();
    void openBuildConfigDialog();
    void syncProjectSettingsBuffersFromDraft();
    void syncProjectSettingsDraftFromBuffers();
    bool loadProjectSettings();
    bool saveProjectSettings();
    void renderProjectSettingsDialog();
    void renderBuildConfigDialog();
    void updateWindowsBuildTask();
    void renderWindowsBuildProgressPopup();
    bool isWindowsBuildRunning() const;
    bool applyGlobalIlluminationWithProgress(Renderer& renderer);
    bool buildWindowsPackageWithProgress();
    bool compileShadersWithProgress();
    bool reloadShadersWithProgress();
    bool compileScriptsWithProgress(const wchar_t* title, bool hotReloadOnly);
    void syncScriptHotReload();
    void updateWindowTitle() const;
    bool confirmSceneChange(const char* actionLabel);
    bool newProjectDialog();
    bool openProjectDialog();
    void newScene();
    bool openSceneDialog();
    bool openSceneFromPath(const std::filesystem::path& path);
    bool saveScene();
    bool saveSceneAs();
    bool saveSceneToPath(const std::filesystem::path& path);
    void duplicateSelectedEntity();
    void deleteSelectedEntity();
    bool enterPlayMode();
    void exitPlayMode();
    bool prepareRuntimeArtifacts(const std::shared_ptr<Scene>& scene);
    std::shared_ptr<Scene> getActiveScene() const;
    std::string getSceneDisplayName() const;
    std::filesystem::path getAssetsRoot() const;

    struct WindowsBuildTaskResult {
        bool success = false;
        std::filesystem::path buildRoot;
        std::filesystem::path executablePath;
        std::string message;
    };

    std::shared_ptr<Scene> m_scene;
    SceneHierarchyPanel m_hierarchy{nullptr};
    PropertiesPanel m_properties;
    ViewportPanel m_viewport;
    ContentBrowserPanel m_contentBrowser{"assets"};
    ConsolePanel m_console;
    StatsPanel m_stats;
    EnvironmentPanel m_environment;
    MaterialEditorPanel m_materialEditor;
    DemonProcessManager m_processManager;

    uint32_t m_fontSrvIndex = UINT32_MAX;
    std::string m_statusText = "Ready";
    int m_statusProgressPercent = -1;
    float m_statusClearTimer = 0.0f;
    float m_processStatusTimer = 0.0f;
    std::string m_projectName = "Untitled Project";
    std::filesystem::path m_projectRoot;
    std::filesystem::path m_projectConfigPath;
    std::filesystem::path m_scenePath;
    ProjectSettings m_projectSettings;
    ProjectSettings m_projectSettingsDraft;
    std::future<WindowsBuildTaskResult> m_windowsBuildFuture;
    std::filesystem::path m_lastWindowsBuildRoot;
    std::filesystem::path m_lastWindowsBuildExecutable;
    std::string m_windowsBuildMessage;
    std::wstring m_windowsBuildStatus = L"Ready";
    std::shared_ptr<Scene> m_runtimeScene;
    std::vector<std::string> m_undoStack;
    std::vector<std::string> m_redoStack;
    std::string m_defaultSceneName = "Default Unitiled Scene";
    bool m_sceneDirty = false;
    bool m_seedSceneOnReady = true;
    bool m_playMode = false;
    bool m_showProjectSettings = false;
    bool m_showBuildConfig = false;
    bool m_windowsBuildPopupOpen = false;
    bool m_windowsBuildCompleted = false;
    bool m_windowsBuildSuccess = false;
    bool m_showHierarchy = true;
    bool m_showProperties = true;
    bool m_showViewport = true;
    bool m_showContentBrowser = true;
    bool m_showConsole = true;
    bool m_showStats = true;
    bool m_showEnvironment = true;
    bool m_showMaterialEditor = true;
    bool m_layoutBuilt = false;
    bool m_paused = false;
    bool m_windowFocusedLastFrame = true;
    float m_windowsBuildProgress = 0.0f;
    std::array<char, 256> m_buildAppNameBuf{};
    std::array<char, 256> m_buildCompanyBuf{};
    std::array<char, 128> m_buildVersionBuf{};
    std::array<char, 256> m_buildOutputBuf{};
    std::array<char, 256> m_buildStartupSceneBuf{};
    std::array<char, 256> m_buildBundleIdBuf{};
    std::array<char, 256> m_buildDirectoryBuf{};
    std::array<char, 512> m_scriptingIdePathBuf{};
};

} // namespace Demon
