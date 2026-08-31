#pragma once
// ==============================================================================
//  DemonEngine Editor::ViewportPanel
//  3D DX12 viewport rendered into an ImGui image.
//  Supports orbit camera, gizmo manipulation via ImGuizmo, and play mode.
// ==============================================================================
#include <imgui.h>
#include "core/DemonPCH.h"
#include "core/EventSystem.h"
#include "renderer/Camera.h"
#include "scene/Scene.h"

namespace Demon {

enum class GizmoMode { None, Translate, Rotate, Scale };
enum class ViewportTab { Scene, Game };

class ViewportPanel {
public:
    ViewportPanel();
    ~ViewportPanel() = default;

    void onUpdate(float dt, const std::shared_ptr<Scene>& scene);
    void render(const std::shared_ptr<Scene>& scene);
    void onEvent(Event& e);

    void setSelectedEntity(Entity e) { m_selected = e; }
    void setPlayMode(bool playing);
    void setPlayPaused(bool paused) { m_playPaused = paused; }
    void setFullscreen(bool enabled);
    bool isFullscreen() const { return m_isFullscreen; }
    const Camera& getCamera() const { return m_editorCamera; }
    bool consumeSceneEdited() { bool v = m_sceneEdited; m_sceneEdited = false; return v; }
    bool consumePlayToggleRequested() { const bool v = m_togglePlayRequested; m_togglePlayRequested = false; return v; }
    bool consumePauseToggleRequested() { const bool v = m_pauseToggleRequested; m_pauseToggleRequested = false; return v; }

    bool getDebugModeEnabled() const { return m_debugModeEnabled; }
    void setDebugModeEnabled(bool enabled) { m_debugModeEnabled = enabled; }
    std::optional<EntityID> consumeSelectionRequest() {
        if (!m_selectionRequestPending)
            return std::nullopt;
        m_selectionRequestPending = false;
        return m_selectionRequest;
    }

private:
    void handleCameraInput(float dt);
    void handleRuntimeInput(float dt, const std::shared_ptr<Scene>& scene);
    void renderGizmo();
    void renderToolbar();
    void renderOverlayStats();
    void renderGameViewOverlay() const;
    void renderViewGizmo();
    void renderRuntimeCrosshair() const;
    void renderDebugBar(const std::shared_ptr<Scene>& scene);
    void renderHoverObjectInfo(const std::shared_ptr<Scene>& scene);
    void drawSelectionOverlay(const std::shared_ptr<Scene>& scene,
                              const ImVec2& viewportPos,
                              const ImVec2& size) const;
    bool resolveSelectedBounds(const std::shared_ptr<Scene>& scene,
                               glm::mat4& outTransform,
                               glm::vec3& outBoundsMin,
                               glm::vec3& outBoundsMax,
                               std::string* outLabel = nullptr) const;
    EntityID pickEntityAtCursor(const std::shared_ptr<Scene>& scene) const;
    EntityID pickEntityAtCenter(const std::shared_ptr<Scene>& scene) const;
    void drawCameraFrustum(const std::shared_ptr<Scene>& scene,
                           const ImVec2& viewportPos,
                           const ImVec2& size);

    Camera      m_editorCamera;
    Entity      m_selected;
    GizmoMode   m_gizmoMode    = GizmoMode::Translate;
    ViewportTab m_activeTab    = ViewportTab::Scene;
    bool        m_localSpace   = true;

    // Viewport framebuffer (rendered into ImGui image)
    uint32_t m_width  = 1280;
    uint32_t m_height = 720;
    ImTextureID m_framebufferTexID = (ImTextureID)0;

    // Orbit camera state
    bool  m_rightMouseHeld = false;
    float m_moveSpeed      = 5.0f;
    float m_sensitivity    = 0.09f;
    float m_lookSmoothness = 14.0f;
    float m_scrollSensitivity = 0.1f;
    glm::vec2 m_smoothedLookDelta{0.0f, 0.0f};
    bool  m_viewportFocused = false;
    bool  m_viewportHovered = false;
    bool  m_sceneEdited = false;
    bool  m_playMode = false;
    bool  m_playPaused = false;
    bool  m_gizmoCapturing = false;
    bool  m_togglePlayRequested = false;
    bool  m_pauseToggleRequested = false;
    bool  m_runtimePointerPrimed = false;
    bool  m_dropTargetHovered = false;
    float m_viewGizmoSize = 96.0f;
    float m_runtimeMoveSpeed = 6.0f;
    float m_runtimeLookSensitivity = 0.16f;
    float m_runtimeEyeHeight = 1.70f;
    float m_runtimeGravity = 22.0f;
    float m_runtimeJumpVelocity = 7.25f;
    float m_runtimeVerticalVelocity = 0.0f;
    glm::vec2 m_runtimeLookSmoothed{0.0f, 0.0f};
    float m_runtimeYaw = 0.0f;
    float m_runtimePitch = 0.0f;
    bool  m_runtimeGrounded = false;
    bool  m_runtimeJumpWasDown = false;
    int   m_resizeCooldownFrames = 0;
    bool  m_selectionRequestPending = false;
    EntityID m_selectionRequest = NULL_ENTITY;
    ImVec2 m_viewportBoundsMin{0.0f, 0.0f};
    ImVec2 m_viewportBoundsMax{0.0f, 0.0f};

    // ── Debug mode ────────────────────────────────────────────────────────────
    bool  m_debugModeEnabled   = false;
    bool  m_isFullscreen       = false;
    // Frames left to force-focus the fullscreen overlay window. ImGui inserts a
    // freshly created NoBringToFrontOnFocus window at the BACK of the draw order
    // (imgui.cpp CreateNewWindow -> g.Windows.push_front), so the overlay has to
    // be focused explicitly once or the dockspace host paints over it.
    int   m_fullscreenFocusFrames = 0;

    // ── FPS tracking ──────────────────────────────────────────────────────────
    float m_fpsAccum           = 0.0f;
    int   m_fpsFrameCount      = 0;
    float m_avgFps             = 0.0f;
    float m_currentFps         = 0.0f;
    float m_dt                 = 0.0f;

    // ── Cached system info (refreshed every ~1s) ──────────────────────────────
    float m_sysRefreshTimer    = 0.0f;
    std::string m_gpuName;
    float m_ramUsedMB          = 0.0f;
    float m_ramTotalMB         = 0.0f;
    float m_ramPercent         = 0.0f;
    float m_vramUsedMB         = 0.0f;
    float m_vramTotalMB        = 0.0f;
    float m_vramPercent        = 0.0f;
    bool  m_vramWarningActive  = false;
    float m_cpuPercent         = 0.0f;
    float m_gpuPercent         = 0.0f;
    float m_engineMemMB        = 0.0f;
    float m_otherAppsMemMB     = 0.0f;

    // ── Hover object info ─────────────────────────────────────────────────────
    EntityID m_hoveredEntity   = NULL_ENTITY;
};

} // namespace Demon
