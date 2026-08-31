#pragma once
// ==============================================================================
//  DemonEngine Editor::ContentBrowserPanel
//  File system browser rooted at the project's asset directory.
//  Supports drag-and-drop to viewport/properties and thumbnail previews.
// ==============================================================================
#include "../engine/core/DemonPCH.h"
#include <filesystem>

namespace Demon {

class MaterialEditorPanel;

struct ContentBrowserModelAssetContents {
    std::filesystem::path sourcePath;
    std::vector<std::string> animationClips;
    std::vector<std::filesystem::path> texturePaths;
    std::vector<std::string> embeddedTextures;
};

class ContentBrowserPanel {
public:
    explicit ContentBrowserPanel(std::filesystem::path rootPath);
    void setMaterialEditor(MaterialEditorPanel* editor) { m_materialEditor = editor; }
    void setRootPath(std::filesystem::path rootPath);
    void render();

private:
    enum class CreateAssetType {
        None,
        Material,
        Folder,
        Script,
        CppScript,
        EmptyFile,
        Shader,
        Scene,
        ReflectionProbe,
    };

    enum class ModelAssetSection {
        Root,
        Animations,
        Textures,
    };

    void renderDirectoryTree(const std::filesystem::path& dir);
    void renderFileGrid();
    void renderModelContentsGrid();
    void renderBreadcrumb();
    void handleDragDrop(const std::filesystem::path& path);
    void handleContextMenuRequest(const std::filesystem::path& targetPath, bool targetIsItem);
    void renderContextMenu();
    void renderCreateAssetPopup();
    void renderRenameAssetPopup();
    void clearModelContentsView();
    void openModelContents(const std::filesystem::path& modelPath);
    void openSelectedPath();
    void showSelectedInExplorer() const;
    void renameSelectedPath();
    void duplicateSelectedPath();
    void deleteSelectedPath();
    void copySelectedPathToClipboard() const;
    void refreshView();
    void importExternalAsset(bool packageMode);
    void createAssetFromPopup();

    const char* getFileIcon(const std::filesystem::path& path, bool isDirectory) const;
    std::filesystem::path getFileIconAsset(const std::filesystem::path& path, bool isDirectory) const;
    [[nodiscard]] bool isModelAssetPath(const std::filesystem::path& path) const;
    [[nodiscard]] std::filesystem::path getActiveDirectoryPath() const;

    std::filesystem::path m_rootPath;
    std::filesystem::path m_currentPath;
    std::filesystem::path m_selected;

    float   m_thumbnailSize = 72.0f;
    float   m_padding       = 8.0f;
    char    m_searchBuf[256] = "";
    int     m_visibleItemCount = 0;

    bool    m_showHiddenFiles = false;
    MaterialEditorPanel* m_materialEditor = nullptr;
    bool    m_openCreateMaterial = false;
    char    m_newMaterialName[128] = "Material1.demon.mat";
    CreateAssetType m_pendingCreateType = CreateAssetType::None;
    bool    m_openCreateAssetPopup = false;
    char    m_newAssetName[128] = "";
    bool    m_contextTargetIsItem = false;
    bool    m_openContextMenu = false;
    bool    m_openRenameAssetPopup = false;
    char    m_renameAssetName[256] = "";
    std::optional<ContentBrowserModelAssetContents> m_modelContents;
    ModelAssetSection m_modelSection = ModelAssetSection::Root;
    std::string m_virtualSelection;
};

} // namespace Demon
