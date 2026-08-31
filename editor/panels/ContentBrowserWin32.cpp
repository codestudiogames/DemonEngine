#include "ContentBrowserPanel.h"

#include "MaterialEditorPanel.h"
#include "../EditorSettings.h"
#include "../utils/FileDialogs.h"
#include "../widgets/EditorIcons.h"
#include "core/Logger.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <shellapi.h>
#include <cctype>
#include <cstdio>

namespace Demon {
namespace {

std::string lowercase(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool matchesFilter(const std::filesystem::path& path, const char* filter)
{
    if (!filter || !*filter)
        return true;
    return lowercase(path.filename().string()).find(lowercase(filter)) != std::string::npos;
}

} // namespace

ContentBrowserPanel::ContentBrowserPanel(std::filesystem::path rootPath)
{
    setRootPath(std::move(rootPath));
}

void ContentBrowserPanel::setRootPath(std::filesystem::path rootPath)
{
    m_rootPath = std::filesystem::absolute(std::move(rootPath)).lexically_normal();
    std::error_code error;
    std::filesystem::create_directories(m_rootPath, error);
    m_currentPath = m_rootPath;
    m_selected.clear();
    clearModelContentsView();
}

void ContentBrowserPanel::render()
{
    ImGui::Begin("Content Browser", nullptr, editorPanelFlags());

    if (ImGui::Button("Import"))
        importExternalAsset(false);
    ImGui::SameLine();
    if (ImGui::Button("Package Import"))
        importExternalAsset(true);
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        refreshView();
    ImGui::SameLine();
    ImGui::Checkbox("Hidden", &m_showHiddenFiles);
    const float searchWidth = 240.0f;
    ImGui::SameLine();
    const float searchSlack = ImGui::GetContentRegionAvail().x - searchWidth;
    if (searchSlack > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + searchSlack);
    ImGui::SetNextItemWidth(searchWidth);
    ImGui::InputTextWithHint("##ContentSearch", "Search assets...", m_searchBuf, sizeof(m_searchBuf));

    renderBreadcrumb();
    ImGui::Separator();

    const float footerHeight = ImGui::GetFrameHeightWithSpacing();
    const float treeWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.24f, 150.0f, 300.0f);
    ImGui::BeginChild("ContentTree", {treeWidth, -footerHeight}, true);
    renderDirectoryTree(m_rootPath);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("ContentGrid", {0.0f, -footerHeight}, true);
    if (m_modelContents)
        renderModelContentsGrid();
    else
        renderFileGrid();

    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
        handleContextMenuRequest(m_currentPath, false);
    // The popup must live in the same window as the OpenPopup() calls above.
    renderContextMenu();
    ImGui::EndChild();

    // Footer: item count + Unity-style thumbnail size slider.
    ImGui::TextDisabled("%d item%s", m_visibleItemCount, m_visibleItemCount == 1 ? "" : "s");
    ImGui::SameLine();
    const float sliderSlack = ImGui::GetContentRegionAvail().x - 150.0f;
    if (sliderSlack > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + sliderSlack);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("##ThumbnailSize", &m_thumbnailSize, 48.0f, 128.0f, "");

    renderCreateAssetPopup();
    renderRenameAssetPopup();
    ImGui::End();
}

void ContentBrowserPanel::renderDirectoryTree(const std::filesystem::path& dir)
{
    std::error_code error;
    const bool selected = dir == m_currentPath;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected)
        flags |= ImGuiTreeNodeFlags_Selected;
    const std::string name = dir.filename().empty() ? dir.string() : dir.filename().string();

    // Empty visible label: the folder icon and name are drawn inline afterwards.
    const float labelStartX = ImGui::GetCursorPosX() + ImGui::GetTreeNodeToLabelSpacing();
    const bool open = ImGui::TreeNodeEx(dir.string().c_str(), flags, "%s", "");
    const bool clicked = ImGui::IsItemClicked();
    const float iconSize = ImGui::GetTextLineHeight();
    if (const ImTextureID folderIcon = EditorIcons::get(EditorIcon::Folder)) {
        ImGui::SameLine(labelStartX);
        ImGui::Image(folderIcon, {iconSize, iconSize});
        ImGui::SameLine(0.0f, 5.0f);
    } else {
        ImGui::SameLine(labelStartX);
    }
    ImGui::TextUnformatted(name.c_str());
    if (clicked) {
        m_currentPath = dir;
        m_selected.clear();
        clearModelContentsView();
    }
    if (!open)
        return;

    std::vector<std::filesystem::directory_entry> directories;
    for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
        if (it->is_directory(error))
            directories.push_back(*it);
    }
    std::ranges::sort(directories, {}, [](const auto& entry) { return lowercase(entry.path().filename().string()); });
    for (const auto& entry : directories)
        renderDirectoryTree(entry.path());
    ImGui::TreePop();
}

void ContentBrowserPanel::renderFileGrid()
{
    std::error_code error;
    std::vector<std::filesystem::directory_entry> entries;
    for (std::filesystem::directory_iterator it(m_currentPath, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (!m_showHiddenFiles && !name.empty() && name.front() == '.')
            continue;
        if (matchesFilter(it->path(), m_searchBuf))
            entries.push_back(*it);
    }
    std::ranges::sort(entries, [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory();
        return lowercase(a.path().filename().string()) < lowercase(b.path().filename().string());
    });
    m_visibleItemCount = static_cast<int>(entries.size());

    const float cellSize = m_thumbnailSize + m_padding;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize));
    if (!ImGui::BeginTable("ContentGridTable", columns))
        return;

    const float labelHeight = ImGui::GetTextLineHeight() + 8.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const auto& entry : entries) {
        ImGui::TableNextColumn();
        ImGui::PushID(entry.path().string().c_str());
        const bool isDirectory = entry.is_directory(error);
        const bool selected = entry.path() == m_selected;
        const std::string name = entry.path().filename().string();

        const ImVec2 tileSize{m_thumbnailSize, m_thumbnailSize + labelHeight};
        const ImVec2 tileMin = ImGui::GetCursorScreenPos();
        const ImVec2 tileMax{tileMin.x + tileSize.x, tileMin.y + tileSize.y};
        ImGui::InvisibleButton("##tile", tileSize);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            m_selected = entry.path();
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_selected = entry.path();
            openSelectedPath();
        }
        if (!isDirectory)
            handleDragDrop(entry.path());
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            handleContextMenuRequest(entry.path(), true);

        // Tile background: soft hover fill, accent ring for the selection.
        const ImVec4 accentColor{0.86f, 0.29f, 0.27f, 1.0f};
        if (selected) {
            ImVec4 fill = accentColor;
            fill.w = 0.16f;
            drawList->AddRectFilled(tileMin, tileMax, ImGui::GetColorU32(fill), 6.0f);
            drawList->AddRect(tileMin, tileMax, ImGui::GetColorU32(accentColor), 6.0f, 0, 1.5f);
        } else if (hovered) {
            drawList->AddRectFilled(tileMin, tileMax, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), 6.0f);
        }

        // Icon, centered in the square area above the label.
        const float iconPad = m_thumbnailSize * 0.10f;
        const ImVec2 iconMin{tileMin.x + iconPad, tileMin.y + iconPad};
        const ImVec2 iconMax{tileMax.x - iconPad, tileMin.y + m_thumbnailSize - iconPad};
        const ImTextureID icon = EditorIcons::get(EditorIcons::iconForPath(entry.path(), isDirectory));
        if (icon) {
            drawList->AddImage(icon, iconMin, iconMax);
        } else {
            const char* tag = getFileIcon(entry.path(), isDirectory);
            const ImVec2 tagSize = ImGui::CalcTextSize(tag);
            drawList->AddText({tileMin.x + (tileSize.x - tagSize.x) * 0.5f,
                               tileMin.y + (m_thumbnailSize - tagSize.y) * 0.5f},
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), tag);
        }

        // Name label: centered when it fits, ellipsized otherwise.
        const ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
        const float labelY = tileMin.y + m_thumbnailSize + 2.0f;
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        if (textSize.x <= tileSize.x - 4.0f) {
            drawList->AddText({tileMin.x + (tileSize.x - textSize.x) * 0.5f, labelY}, textColor, name.c_str());
        } else {
            ImGui::RenderTextEllipsis(drawList, {tileMin.x + 2.0f, labelY},
                                      {tileMax.x - 2.0f, labelY + ImGui::GetTextLineHeight()},
                                      tileMax.x - 2.0f, name.c_str(), nullptr, &textSize);
            if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                ImGui::SetTooltip("%s", name.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ContentBrowserPanel::renderModelContentsGrid()
{
    if (ImGui::Button("<- Back to Files")) {
        clearModelContentsView();
        return;
    }
    ImGui::Separator();
    ImGui::Text("Model: %s", m_modelContents->sourcePath.filename().string().c_str());
    ImGui::Text("Animations: %zu", m_modelContents->animationClips.size());
    ImGui::Text("Textures: %zu", m_modelContents->texturePaths.size() + m_modelContents->embeddedTextures.size());
    for (const auto& clip : m_modelContents->animationClips)
        ImGui::BulletText("%s", clip.c_str());
}

void ContentBrowserPanel::renderBreadcrumb()
{
    if (ImGui::Button("Assets")) {
        m_currentPath = m_rootPath;
        clearModelContentsView();
    }
    std::filesystem::path running = m_rootPath;
    const auto relative = m_currentPath.lexically_relative(m_rootPath);
    for (const auto& part : relative) {
        running /= part;
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        const std::string label = part.string();
        if (ImGui::SmallButton(label.c_str())) {
            m_currentPath = running;
            clearModelContentsView();
        }
    }
}

void ContentBrowserPanel::handleDragDrop(const std::filesystem::path& path)
{
    if (!ImGui::BeginDragDropSource())
        return;
    const std::string text = path.string();
    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", text.c_str(), text.size() + 1);
    ImGui::TextUnformatted(path.filename().string().c_str());
    ImGui::EndDragDropSource();
}

void ContentBrowserPanel::handleContextMenuRequest(const std::filesystem::path& targetPath, bool targetIsItem)
{
    m_contextTargetIsItem = targetIsItem;
    if (targetIsItem) {
        m_selected = targetPath;
    } else {
        m_selected.clear();
        m_currentPath = targetPath;
    }
    // Defer OpenPopup to renderContextMenu so it runs in the same ID scope as
    // BeginPopup (requests can originate inside PushID blocks).
    m_openContextMenu = true;
}

void ContentBrowserPanel::renderContextMenu()
{
    if (m_openContextMenu) {
        m_openContextMenu = false;
        ImGui::OpenPopup("ContentBrowserContext");
    }
    if (ImGui::BeginPopup("ContentBrowserContext")) {
        if (ImGui::MenuItem("Open", nullptr, false, m_contextTargetIsItem))
            openSelectedPath();
        if (ImGui::MenuItem("Import Files..."))
            importExternalAsset(false);
        if (ImGui::MenuItem("Package Import..."))
            importExternalAsset(true);
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Folder")) { m_pendingCreateType = CreateAssetType::Folder; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("Material")) { m_pendingCreateType = CreateAssetType::Material; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("Demon Script")) { m_pendingCreateType = CreateAssetType::Script; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("C++ Script")) { m_pendingCreateType = CreateAssetType::CppScript; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("Shader")) { m_pendingCreateType = CreateAssetType::Shader; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("Scene")) { m_pendingCreateType = CreateAssetType::Scene; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("Empty File...")) { m_pendingCreateType = CreateAssetType::EmptyFile; m_openCreateAssetPopup = true; }
            if (ImGui::MenuItem("Reflection Probe")) { m_pendingCreateType = CreateAssetType::ReflectionProbe; m_openCreateAssetPopup = true; }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Refresh"))
            refreshView();
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer"))
            showSelectedInExplorer();
        if (ImGui::MenuItem("Copy Path", nullptr, false, m_contextTargetIsItem))
            copySelectedPathToClipboard();
        if (ImGui::MenuItem("Rename", nullptr, false, m_contextTargetIsItem)) {
            std::snprintf(m_renameAssetName, sizeof(m_renameAssetName), "%s",
                          m_selected.filename().string().c_str());
            m_openRenameAssetPopup = true;
        }
        if (ImGui::MenuItem("Duplicate", nullptr, false, m_contextTargetIsItem))
            duplicateSelectedPath();
        if (ImGui::MenuItem("Delete", nullptr, false, m_contextTargetIsItem))
            deleteSelectedPath();
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::renderCreateAssetPopup()
{
    if (m_openCreateAssetPopup) {
        m_openCreateAssetPopup = false;
        const char* defaultName = "New Asset";
        switch (m_pendingCreateType) {
            case CreateAssetType::Folder: defaultName = "New Folder"; break;
            case CreateAssetType::Material: defaultName = "New Material.demon.mat"; break;
            case CreateAssetType::Script: defaultName = "NewBehavior.demon.cs"; break;
            case CreateAssetType::CppScript: defaultName = "NewScript.cpp"; break;
            case CreateAssetType::EmptyFile: defaultName = "NewFile.txt"; break;
            case CreateAssetType::Shader: defaultName = "NewShader.hlsl"; break;
            case CreateAssetType::Scene: defaultName = "NewScene.demon"; break;
            case CreateAssetType::ReflectionProbe: defaultName = "New Probe.demonprobe"; break;
            default: break;
        }
        std::snprintf(m_newAssetName, sizeof(m_newAssetName), "%s", defaultName);
        ImGui::OpenPopup("Create Asset");
    }
    if (ImGui::BeginPopupModal("Create Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_newAssetName, sizeof(m_newAssetName));
        if (ImGui::Button("Create")) {
            createAssetFromPopup();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ContentBrowserPanel::renderRenameAssetPopup()
{
    if (m_openRenameAssetPopup) {
        m_openRenameAssetPopup = false;
        ImGui::OpenPopup("Rename Asset");
    }
    if (!ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("New name", m_renameAssetName, sizeof(m_renameAssetName));
    if (ImGui::Button("Rename")) {
        renameSelectedPath();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ContentBrowserPanel::clearModelContentsView()
{
    m_modelContents.reset();
    m_modelSection = ModelAssetSection::Root;
    m_virtualSelection.clear();
}

void ContentBrowserPanel::openModelContents(const std::filesystem::path& modelPath)
{
    m_modelContents = ContentBrowserModelAssetContents{.sourcePath = modelPath};
}

void ContentBrowserPanel::openSelectedPath()
{
    if (m_selected.empty())
        return;
    std::error_code error;
    if (std::filesystem::is_directory(m_selected, error)) {
        m_currentPath = m_selected;
        clearModelContentsView();
        return;
    }
    if (isModelAssetPath(m_selected)) {
        openModelContents(m_selected);
        return;
    }
    const std::string ext = lowercase(m_selected.extension().string());
    if (m_materialEditor && (ext == ".mat" || ext == ".material" || m_selected.string().ends_with(".demon.mat"))) {
        m_materialEditor->openMaterial(m_selected);
        return;
    }
    ShellExecuteA(nullptr, "open", m_selected.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ContentBrowserPanel::showSelectedInExplorer() const
{
    const auto path = m_selected.empty() ? m_currentPath : m_selected;
    const std::string args = "/select,\"" + path.string() + "\"";
    ShellExecuteA(nullptr, "open", "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void ContentBrowserPanel::renameSelectedPath()
{
    if (m_selected.empty() || m_selected.lexically_normal() == m_rootPath.lexically_normal())
        return;
    const std::string name = m_renameAssetName;
    if (name.empty() || name == "." || name == ".." || name.find_first_of("\\/") != std::string::npos)
        return;

    const auto destination = m_selected.parent_path() / name;
    if (std::filesystem::exists(destination)) {
        MessageBoxA(GetActiveWindow(), "An asset with that name already exists.", "Content Browser", MB_ICONWARNING | MB_OK);
        return;
    }

    std::error_code error;
    std::filesystem::rename(m_selected, destination, error);
    if (error) {
        DEMON_LOG_ERROR("Content Browser rename failed: {}", error.message());
        MessageBoxA(GetActiveWindow(), error.message().c_str(), "Content Browser", MB_ICONERROR | MB_OK);
        return;
    }
    m_selected = destination;
    refreshView();
}

void ContentBrowserPanel::duplicateSelectedPath()
{
    if (m_selected.empty() || m_selected.lexically_normal() == m_rootPath.lexically_normal())
        return;

    std::error_code error;
    const bool directory = std::filesystem::is_directory(m_selected, error);
    if (error)
        return;
    const std::string base = m_selected.stem().string() + " Copy";
    const std::string extension = directory ? std::string{} : m_selected.extension().string();
    auto destination = m_selected.parent_path() / (base + extension);
    for (int suffix = 2; std::filesystem::exists(destination); ++suffix)
        destination = m_selected.parent_path() / (base + " " + std::to_string(suffix) + extension);

    if (directory) {
        std::filesystem::copy(m_selected, destination, std::filesystem::copy_options::recursive, error);
    } else {
        std::filesystem::copy_file(m_selected, destination, std::filesystem::copy_options::none, error);
    }
    if (error) {
        DEMON_LOG_ERROR("Content Browser duplicate failed: {}", error.message());
        MessageBoxA(GetActiveWindow(), error.message().c_str(), "Content Browser", MB_ICONERROR | MB_OK);
        return;
    }
    m_selected = destination;
    refreshView();
}

void ContentBrowserPanel::deleteSelectedPath()
{
    if (m_selected.empty() || m_selected.lexically_normal() == m_rootPath.lexically_normal())
        return;
    if (MessageBoxA(GetActiveWindow(), "Delete the selected asset?", "Content Browser", MB_ICONWARNING | MB_YESNO) != IDYES)
        return;
    std::error_code error;
    if (std::filesystem::is_directory(m_selected, error))
        std::filesystem::remove_all(m_selected, error);
    else
        std::filesystem::remove(m_selected, error);
    if (error)
        DEMON_LOG_ERROR("Content Browser delete failed: {}", error.message());
    m_selected.clear();
}

void ContentBrowserPanel::copySelectedPathToClipboard() const
{
    if (m_selected.empty() || !OpenClipboard(GetActiveWindow()))
        return;
    EmptyClipboard();
    const std::string text = m_selected.string();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (memory) {
        memcpy(GlobalLock(memory), text.c_str(), text.size() + 1);
        GlobalUnlock(memory);
        SetClipboardData(CF_TEXT, memory);
    }
    CloseClipboard();
}

void ContentBrowserPanel::refreshView()
{
    if (!std::filesystem::exists(m_currentPath))
        m_currentPath = m_rootPath;
}

void ContentBrowserPanel::importExternalAsset(bool)
{
    const auto source = FileDialogs::openFile("All Supported Assets\0*.fbx;*.obj;*.gltf;*.glb;*.png;*.jpg;*.wav;*.ogg\0All Files\0*.*\0");
    if (!source)
        return;
    std::error_code error;
    const auto destination = m_currentPath / std::filesystem::path(*source).filename();
    std::filesystem::copy_file(*source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error)
        DEMON_LOG_ERROR("Asset import failed: {}", error.message());
}

void ContentBrowserPanel::createAssetFromPopup()
{
    const std::string name = m_newAssetName;
    if (name.empty())
        return;
    const auto path = m_currentPath / name;
    std::error_code error;
    if (m_pendingCreateType == CreateAssetType::Folder) {
        std::filesystem::create_directories(path, error);
    } else if (m_pendingCreateType == CreateAssetType::Scene) {
        SceneSerializer serializer(Scene::create(path.stem().string()));
        if (!serializer.serialize(path.string()))
            error = std::make_error_code(std::errc::io_error);
    } else {
        std::ofstream file(path);
        if (m_pendingCreateType == CreateAssetType::Material)
            file << "{\n  \"albedo\": [1, 1, 1, 1],\n  \"metallic\": 0,\n  \"roughness\": 0.5\n}\n";
        else if (m_pendingCreateType == CreateAssetType::Script)
            file << "behavior NewBehavior {\n    on update(dt) {\n    }\n}\n";
        else if (m_pendingCreateType == CreateAssetType::CppScript)
            file << "#include \"core/DemonPCH.h\"\n\nnamespace Demon {\n\n// Script implementation.\n\n} // namespace Demon\n";
        else if (m_pendingCreateType == CreateAssetType::Shader)
            file << "float4 main() : SV_Target0\n{\n    return float4(1.0, 1.0, 1.0, 1.0);\n}\n";
        else if (m_pendingCreateType == CreateAssetType::ReflectionProbe)
            file << "{\n  \"intensity\": 1.0,\n  \"blendDistance\": 1.0\n}\n";
        if (!file)
            error = std::make_error_code(std::errc::io_error);
    }
    if (error)
        DEMON_LOG_ERROR("Asset creation failed: {}", error.message());
    m_pendingCreateType = CreateAssetType::None;
}

const char* ContentBrowserPanel::getFileIcon(const std::filesystem::path& path, bool isDirectory) const
{
    if (isDirectory) return "[DIR]";
    const std::string ext = lowercase(path.extension().string());
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") return "[3D]";
    if (ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".hdr") return "[IMG]";
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return "[SND]";
    if (ext == ".cs" || ext == ".lua") return "[CODE]";
    return "[FILE]";
}

std::filesystem::path ContentBrowserPanel::getFileIconAsset(const std::filesystem::path&, bool) const
{
    return {};
}

bool ContentBrowserPanel::isModelAssetPath(const std::filesystem::path& path) const
{
    const std::string ext = lowercase(path.extension().string());
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae";
}

std::filesystem::path ContentBrowserPanel::getActiveDirectoryPath() const
{
    return m_currentPath;
}

} // namespace Demon
