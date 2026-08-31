#pragma once
// ==============================================================================
//  DemonEngine Editor::MaterialEditorPanel
//  Material editor that lives inside a native Win32 shell window.
//  ImGui content is pinned to the Win32 client area every frame - no
//  multi-viewport needed, no second DX12 device, no second swapchain.
// ==============================================================================
#include <imgui.h>
#include "../engine/core/DemonPCH.h"
#include "scene/Scene.h"
#include "scene/Components.h"
#include <filesystem>

namespace Demon {

class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();

    void render();
    bool handleShortcutSave();

    // Open / create material docs
    void openMaterial(const std::filesystem::path& path);
    void openMaterialForEntity(const std::filesystem::path& path, Scene* scene, EntityID id);
    bool createAndOpen(const std::filesystem::path& path);
    static bool loadFromFile(const std::filesystem::path& path, MaterialComponent& out);

private:
    // ── Per-document state ────────────────────────────────────────────────────
    struct MaterialDoc {
        std::filesystem::path path;
        MaterialComponent     data;
        bool  open         = true;
        bool  dirty        = false;
        bool  focused      = false;
        bool  requestFocus = false;
        Scene*   linkedScene  = nullptr;
        EntityID linkedEntity = NULL_ENTITY;
        bool  previewDirty = true;
        ImTextureID previewId = 0;
        std::shared_ptr<class Texture> previewTexture;
    };

    // ── Native Win32 shell window ─────────────────────────────────────────────
    HWND   m_shellHwnd    = nullptr;   // the real Win32 window
    bool   m_shellVisible = false;
    int    m_shellW = 520, m_shellH = 760;

    // Win32 window class registration (done once)
    static bool s_classRegistered;
    static LRESULT CALLBACK shellWndProc(HWND, UINT, WPARAM, LPARAM);
    void createShell();
    void destroyShell();
    void showShell(bool show);
    // Returns the ImGui screen-space rect that maps to the Win32 client area
    bool getShellClientRect(float& x, float& y, float& w, float& h) const;

    // ── Doc management ────────────────────────────────────────────────────────
    std::vector<MaterialDoc> m_docs;
    MaterialDoc* findDoc(const std::filesystem::path& path);
    void renderDoc(MaterialDoc& doc);
    void markDirty(MaterialDoc& doc) { doc.dirty = true; doc.previewDirty = true; }
    void syncLinkedEntity(MaterialDoc& doc);
    void updatePreview(MaterialDoc& doc);

    static bool saveToFile(const std::filesystem::path& path, const MaterialComponent& data);
    static bool isImageFile(const std::filesystem::path& path);
};

} // namespace Demon
