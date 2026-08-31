#pragma once
// ==============================================================================
//  DemonEngine Editor::EditorIcons
//  Rasterizes the SVG icon set in assets/icons with nanosvg and uploads the
//  results as DX12 textures so editor panels can draw crisp asset icons.
// ==============================================================================
#include <imgui.h>
#include <filesystem>

namespace Demon {

enum class EditorIcon {
    Folder,
    File,
    Image,
    Mesh,
    Material,
    Scene,
    Script,
    Shader,
    Count,
};

class EditorIcons {
public:
    // Loads every icon texture. Call once after the renderer is initialized.
    static void init();
    // Releases the icon textures. Call while the GPU is idle, before the
    // renderer shuts down.
    static void shutdown();

    // Returns 0 when the icon (or its SVG source) is unavailable.
    static ImTextureID get(EditorIcon icon);

    // Maps an asset path (or directory flag) to the icon that represents it.
    static EditorIcon iconForPath(const std::filesystem::path& path, bool isDirectory);
};

} // namespace Demon
