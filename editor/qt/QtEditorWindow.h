#pragma once

#include "scene/Scene.h"

#include <QMainWindow>
#include <filesystem>
#include <memory>

class QFileSystemModel;
class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QTreeView;
class QTreeWidget;
class QCloseEvent;
class QPoint;

namespace Demon {

class Application;
class EditorRuntimeLayer;
class RhiViewportWindow;
class QtComponentsPanel;

class QtEditorWindow final : public QMainWindow {
public:
    QtEditorWindow(std::string projectName,
                   std::filesystem::path projectRoot,
                   std::filesystem::path projectConfig);
    ~QtEditorWindow() override;

    [[nodiscard]] HWND viewportHandle() const;
    void bindRuntime(Application* application, EditorRuntimeLayer* runtime);
    void setScene(std::shared_ptr<Scene> scene);
    [[nodiscard]] const std::shared_ptr<Scene>& scene() const { return m_scene; }
    void refreshAll();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void buildToolbar();
    void buildDocks();
    void applyTheme();
    void seedDefaultScene();
    void rebuildHierarchy();
    void rebuildInspector();
    void rebuildMaterialEditor();
    void refreshStats();
    void selectEntity(EntityID id);
    void showViewportContextMenu(const QPoint& globalPosition);
    void showContentBrowserContextMenu(const QPoint& position);
    [[nodiscard]] std::filesystem::path contentBrowserDirectory() const;
    [[nodiscard]] std::filesystem::path selectedContentPath() const;
    void createContentFolder();
    void createContentFile(const std::string& suggestedName, const std::string& contents);
    void renameContentItem();
    void duplicateContentItem();
    void deleteContentItem();
    void importContentFiles();
    void refreshContentBrowser();
    void newScene();
    void openScene();
    bool saveScene(bool saveAs);
    void createEntity(const QString& name);
    void deleteSelectedEntity();
    void toggleRuntime();
    void togglePause();
    void showProjectSettings();
    void showBuildProgress();

    std::string m_projectName;
    std::filesystem::path m_projectRoot;
    std::filesystem::path m_projectConfig;
    std::filesystem::path m_assetsRoot;
    std::filesystem::path m_scenePath;
    std::shared_ptr<Scene> m_scene;
    Application* m_application = nullptr;
    EditorRuntimeLayer* m_runtime = nullptr;
    RhiViewportWindow* m_viewport = nullptr;
    QWidget* m_viewportContainer = nullptr;
    QTreeWidget* m_hierarchy = nullptr;
    QtComponentsPanel* m_inspector = nullptr;
    QTreeView* m_contentBrowser = nullptr;
    QFileSystemModel* m_fileModel = nullptr;
    QPlainTextEdit* m_console = nullptr;
    QLabel* m_stats = nullptr;
    QScrollArea* m_materialEditor = nullptr;
    EntityID m_selectedEntity = NULL_ENTITY;
    uint64_t m_loggerSinkId = 0;
    bool m_sceneDirty = false;
};

} // namespace Demon
