#pragma once
// =============================================================================
//  DemonGUI Editor Panels  —  forward declarations + base class
// =============================================================================
#include "DemonGUI/GUIContext.h"
#include "DemonGUI/GUITypes.h"
#include "components/ComponentEditors.h"
#include "scene/Scene.h"
#include "scene/Components.h"
#include "renderer/Renderer.h"

namespace Demon { class EditorRuntimeLayer; }

namespace Demon::GUI {

// ── Base panel ────────────────────────────────────────────────────────────────
class EditorPanel {
public:
    virtual ~EditorPanel() = default;
    virtual void render(GUIContext& ctx, Rect content) = 0;
    virtual void onEvent(Event&) {}
};

// ─────────────────────────────────────────────────────────────────────────────
//  ViewportPanel  —  displays the DX12 offscreen render target
// ─────────────────────────────────────────────────────────────────────────────
class ViewportPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void setRenderer(Renderer* r) { m_renderer = r; }
    void setScene(std::shared_ptr<Scene> s) { m_scene = s; }
    void setRuntime(Demon::EditorRuntimeLayer* runtime) { m_runtime = runtime; }
    [[nodiscard]] bool contains(Vec2 point) const { return m_content.contains(point); }
    [[nodiscard]] glm::vec2 screenToNdc(Vec2 point) const;

    // Returns last-known viewport size (for renderer resize)
    Vec2 viewportSize() const { return {float(m_lastW), float(m_lastH)}; }

private:
    Renderer*             m_renderer = nullptr;
    Demon::EditorRuntimeLayer* m_runtime = nullptr;
    std::shared_ptr<Scene> m_scene;
    Rect m_content{};
    uint32_t m_lastW = 0, m_lastH = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  SceneHierarchyPanel
// ─────────────────────────────────────────────────────────────────────────────
class SceneHierarchyPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void setScene(std::shared_ptr<Scene> s) { m_scene = s; }
    Entity selectedEntity() const { return m_selected; }
    void setSelectedEntity(EntityID id) { m_selected = m_scene ? m_scene->getEntityByID(id) : Entity{}; }

private:
    void drawEntity(GUIContext& ctx, Entity e, float& y, Rect content, int depth);

    std::shared_ptr<Scene> m_scene;
    Entity m_selected;
    float  m_scrollY  = 0.f;
    std::unordered_map<uint64_t, bool> m_open;
};

// ─────────────────────────────────────────────────────────────────────────────
//  PropertiesPanel
// ─────────────────────────────────────────────────────────────────────────────
class PropertiesPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void setSelectedEntity(Entity e) { m_entity = e; }

private:
    void drawTransform  (GUIContext& ctx, float& y, Rect r, TransformComponent& tc);
    void drawMeshRenderer(GUIContext& ctx, float& y, Rect r, MeshRendererComponent& mr);
    void drawCamera     (GUIContext& ctx, float& y, Rect r, CameraComponent& cc);
    void drawLight      (GUIContext& ctx, float& y, Rect r, LightComponent& lc);
    void drawFog        (GUIContext& ctx, float& y, Rect r, FogComponent& fc);

    Entity m_entity;
    Demon::Editor::ComponentEditors m_componentEditors;
    float  m_scrollY = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ContentBrowserPanel
// ─────────────────────────────────────────────────────────────────────────────
class ContentBrowserPanel : public EditorPanel {
public:
    ContentBrowserPanel();
    void render(GUIContext& ctx, Rect content) override;
    void setRootPath(const std::string& path);

private:
    void drawDirectory(GUIContext& ctx, const std::filesystem::path& dir,
                       float& y, Rect content, int depth);

    std::filesystem::path m_root;
    std::filesystem::path m_current;
    std::unordered_map<std::string, bool> m_dirOpen;
    float m_scrollY = 0.f;
    std::string m_filter;
    bool m_contextMenuOpen = false;
    Vec2 m_contextMenuPos{};
    uint32_t m_createCounter = 1;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ConsolePanel
// ─────────────────────────────────────────────────────────────────────────────
struct ConsoleEntry {
    std::string message;
    int         level;   // 0=info 1=warn 2=error
};

class ConsolePanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void pushMessage(const std::string& msg, int level = 0);
    void clear() { m_entries.clear(); }

private:
    std::vector<ConsoleEntry> m_entries;
    float       m_scrollY   = 0.f;
    bool        m_autoScroll = true;
    bool        m_showInfo   = true;
    bool        m_showWarn   = true;
    bool        m_showError  = true;
    std::string m_filter;
};

// ─────────────────────────────────────────────────────────────────────────────
//  EnvironmentPanel  —  lighting, skybox, fog
// ─────────────────────────────────────────────────────────────────────────────
struct EnvironmentSettings {
    // Directional light
    float lightDir[3]   = {0.4f, -1.0f, 0.3f};
    float lightColor[3] = {1.0f,  1.0f, 1.0f};
    float lightIntensity = 1.0f;
    // Ambient
    float ambient[3]    = {0.25f, 0.25f, 0.25f};
    // Skybox
    bool  skyEnabled    = false;
    char  skyPath[256]  = "";
    float skyIntensity  = 1.0f;
    // Fog (shared with FogComponent)
    bool  fogEnabled    = false;
    float fogColor[3]   = {0.5f, 0.6f, 0.7f};
    float fogDensity    = 0.02f;
    float fogStart      = 10.f;
    float fogHeight     = 0.f;
    float fogFalloff    = 1.f;
};

class EnvironmentPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void setRenderer(Renderer* r) { m_renderer = r; }
    const EnvironmentSettings& settings() const { return m_env; }

private:
    Renderer*           m_renderer = nullptr;
    EnvironmentSettings m_env{};
    float               m_scrollY  = 0.f;
    bool                m_lightOpen= true;
    bool                m_skyOpen  = true;
    bool                m_fogOpen  = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  VolumetricsPanel  —  volumetric clouds + fog
// ─────────────────────────────────────────────────────────────────────────────
struct VolumetricsSettings {
    // Volumetric fog
    bool  volFogEnabled   = false;
    float volFogDensity   = 0.05f;
    float volFogScatter   = 0.5f;
    float volFogColor[3]  = {0.8f, 0.85f, 0.9f};
    float volFogHeight    = 50.f;
    float volFogFalloff   = 0.01f;
    // Clouds
    bool  cloudsEnabled   = false;
    float cloudCoverage   = 0.5f;
    float cloudDensity    = 0.3f;
    float cloudSpeed      = 0.01f;
    float cloudAltitude   = 500.f;
    float cloudThickness  = 100.f;
    float cloudColor[3]   = {1.0f, 0.98f, 0.95f};
    float cloudShadow     = 0.5f;
    // Sun shafts / god rays
    bool  godRaysEnabled  = false;
    float godRaysIntensity= 0.5f;
    float godRaysDecay    = 0.97f;
    float godRaysDensity  = 0.5f;
};

class VolumetricsPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    const VolumetricsSettings& settings() const { return m_vol; }

private:
    VolumetricsSettings m_vol{};
    float m_scrollY      = 0.f;
    bool  m_volFogOpen   = true;
    bool  m_cloudsOpen   = true;
    bool  m_godRaysOpen  = false;
};

class MaterialEditorPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void setScene(std::shared_ptr<Scene> scene) { m_scene = std::move(scene); }

private:
    std::shared_ptr<Scene> m_scene;
};

class StatsPanel : public EditorPanel {
public:
    void render(GUIContext& ctx, Rect content) override;
    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

private:
    Renderer* m_renderer = nullptr;
};

} // namespace Demon::GUI
