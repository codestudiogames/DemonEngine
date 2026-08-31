// ==============================================================================
//  DemonEngine Editor::EditorIcons  –  Implementation
//  SVG icons are rasterized once at startup with nanosvg and cached as DX12
//  textures for the lifetime of the editor.
// ==============================================================================
#include "EditorIcons.h"

#include "core/Application.h"
#include "core/Logger.h"
#include "renderer/Renderer.h"
#include "renderer/Texture.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <array>
#include <cctype>
#include <vector>

namespace Demon {
namespace {

constexpr int k_iconPixelSize = 128;

struct IconSlot {
    std::shared_ptr<Texture> texture;
    ImTextureID textureId = 0;
};

std::array<IconSlot, static_cast<size_t>(EditorIcon::Count)> g_icons;
bool g_initialized = false;

const char* iconFileName(EditorIcon icon)
{
    switch (icon) {
        case EditorIcon::Folder:   return "folder_icon_simple_white.svg";
        case EditorIcon::File:     return "file_icon_simple_white.svg";
        case EditorIcon::Image:    return "image_file_icon_simple_white.svg";
        case EditorIcon::Mesh:     return "demonmesh_file_icon.svg";
        case EditorIcon::Material: return "demonmat_file_icon.svg";
        case EditorIcon::Scene:    return "demonscene_file_icon.svg";
        case EditorIcon::Script:   return "demonscript_file_icon.svg";
        case EditorIcon::Shader:   return "demonshader_file_icon.svg";
        default:                   return nullptr;
    }
}

std::filesystem::path executableDirectory()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path resolveIconsDirectory()
{
    std::error_code error;
    const std::filesystem::path candidates[] = {
        std::filesystem::current_path(error) / "assets" / "icons",
        executableDirectory() / "assets" / "icons",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, error))
            return candidate;
    }
    return {};
}

std::shared_ptr<Texture> rasterizeSvg(const std::filesystem::path& path, NSVGrasterizer* rasterizer)
{
    NSVGimage* image = nsvgParseFromFile(path.string().c_str(), "px", 96.0f);
    if (!image || image->width <= 0.0f || image->height <= 0.0f) {
        if (image)
            nsvgDelete(image);
        DEMON_LOG_WARN("EditorIcons: failed to parse SVG '{}'.", path.string());
        return nullptr;
    }

    // Uniform scale-to-fit inside a square canvas, centered.
    const float scale = static_cast<float>(k_iconPixelSize) / std::max(image->width, image->height);
    const float tx = (k_iconPixelSize - image->width * scale) * 0.5f;
    const float ty = (k_iconPixelSize - image->height * scale) * 0.5f;

    std::vector<uint8_t> pixels(static_cast<size_t>(k_iconPixelSize) * k_iconPixelSize * 4, 0);
    nsvgRasterize(rasterizer, image, tx, ty, scale, pixels.data(), k_iconPixelSize, k_iconPixelSize, k_iconPixelSize * 4);
    nsvgDelete(image);

    auto& renderer = Application::get().getRenderer();
    return Texture::createFromRGBA(pixels.data(), k_iconPixelSize, k_iconPixelSize,
                                   renderer.getContext(), renderer.getSrvHeap(), false);
}

std::string lowercaseExtension(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

void EditorIcons::init()
{
    if (g_initialized)
        return;
    g_initialized = true;

    const auto iconsDir = resolveIconsDirectory();
    if (iconsDir.empty()) {
        DEMON_LOG_WARN("EditorIcons: assets/icons directory not found; falling back to text icons.");
        return;
    }

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer)
        return;

    int loaded = 0;
    for (size_t i = 0; i < g_icons.size(); ++i) {
        const char* fileName = iconFileName(static_cast<EditorIcon>(i));
        if (!fileName)
            continue;
        const auto path = iconsDir / fileName;
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            continue;
        auto texture = rasterizeSvg(path, rasterizer);
        if (texture && texture->getSrvGpuHandle().ptr != 0) {
            g_icons[i].texture = std::move(texture);
            g_icons[i].textureId = static_cast<ImTextureID>(g_icons[i].texture->getSrvGpuHandle().ptr);
            ++loaded;
        }
    }
    nsvgDeleteRasterizer(rasterizer);
    DEMON_LOG_INFO("EditorIcons: {} SVG icons rasterized from '{}'.", loaded, iconsDir.string());
}

void EditorIcons::shutdown()
{
    for (auto& slot : g_icons)
        slot = {};
    g_initialized = false;
}

ImTextureID EditorIcons::get(EditorIcon icon)
{
    const auto index = static_cast<size_t>(icon);
    if (index >= g_icons.size())
        return 0;
    return g_icons[index].textureId;
}

EditorIcon EditorIcons::iconForPath(const std::filesystem::path& path, bool isDirectory)
{
    if (isDirectory)
        return EditorIcon::Folder;

    const std::string fileName = [&] {
        std::string name = path.filename().string();
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name;
    }();
    const std::string ext = lowercaseExtension(path);

    if (fileName.ends_with(".demon.mat") || ext == ".mat" || ext == ".material" || ext == ".demonmat")
        return EditorIcon::Material;
    if (fileName.ends_with(".demon.cs") || ext == ".cs" || ext == ".lua" || ext == ".demonscript" ||
        ext == ".cpp" || ext == ".h" || ext == ".hpp")
        return EditorIcon::Script;
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".demonmesh")
        return EditorIcon::Mesh;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".hdr" ||
        ext == ".tga" || ext == ".bmp")
        return EditorIcon::Image;
    if (ext == ".demon" || ext == ".demonscene")
        return EditorIcon::Scene;
    if (ext == ".hlsl" || ext == ".glsl" || ext == ".shader" || ext == ".cso" || ext == ".demonshader")
        return EditorIcon::Shader;
    return EditorIcon::File;
}

} // namespace Demon
