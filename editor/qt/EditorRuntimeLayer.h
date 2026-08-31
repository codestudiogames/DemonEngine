#pragma once

#include "core/Layer.h"
#include "renderer/Camera.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"
#include "scene/Components.h"
#include "scene/Scene.h"

namespace Demon {

class EditorRuntimeLayer final : public Layer {
public:
    enum class TransformTool { Translate, Rotate, Scale };

    explicit EditorRuntimeLayer(std::shared_ptr<Scene> scene);

    void onUpdate(float dt) override;
    void onRender(Renderer& renderer) override;

    void setScene(std::shared_ptr<Scene> scene);
    [[nodiscard]] const std::shared_ptr<Scene>& scene() const { return m_scene; }
    void setPlaying(bool playing);
    [[nodiscard]] bool isPlaying() const { return m_playing; }
    void setPaused(bool paused) { m_paused = paused; }
    [[nodiscard]] bool isPaused() const { return m_paused; }
    void setSelectedEntity(EntityID id) { m_selectedEntity = id; }
    [[nodiscard]] EntityID pickEntity(float ndcX, float ndcY) const;
    [[nodiscard]] bool beginTransformDrag(float ndcX, float ndcY);
    [[nodiscard]] bool updateTransformDrag(float ndcX, float ndcY);
    void endTransformDrag();
    [[nodiscard]] bool isTransformDragging() const { return m_transformDragging; }
    void focusSelected();
    void setTransformTool(TransformTool tool) { m_transformTool = tool; }
    [[nodiscard]] TransformTool transformTool() const { return m_transformTool; }
    void setGridVisible(bool visible) { m_gridVisible = visible; }
    [[nodiscard]] bool isGridVisible() const { return m_gridVisible; }
    void setSnapEnabled(bool enabled) { m_snapEnabled = enabled; }
    [[nodiscard]] bool isSnapEnabled() const { return m_snapEnabled; }

private:
    void updateEditorCamera(float dt);
    void renderEditorGrid(Renderer& renderer);
    void renderTransformGizmo(Renderer& renderer);

    std::shared_ptr<Scene> m_scene;
    std::shared_ptr<Scene> m_editScene;
    Camera m_editorCamera{55.0f, 16.0f / 9.0f, 0.05f, 2000.0f};
    glm::vec3 m_cameraPosition{0.0f, 3.5f, 8.0f};
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = -18.0f;
    uint32_t m_lastWidth = 0;
    uint32_t m_lastHeight = 0;
    EntityID m_selectedEntity = NULL_ENTITY;
    std::shared_ptr<Mesh> m_editorCube;
    Material m_gridMinorMaterial{"EditorGridMinor"};
    Material m_gridMajorMaterial{"EditorGridMajor"};
    Material m_gizmoXMaterial{"EditorGizmoX"};
    Material m_gizmoYMaterial{"EditorGizmoY"};
    Material m_gizmoZMaterial{"EditorGizmoZ"};
    TransformTool m_transformTool = TransformTool::Translate;
    bool m_gridVisible = true;
    bool m_snapEnabled = false;
    bool m_transformDragging = false;
    int m_activeAxis = -1;
    glm::vec2 m_dragStartNdc{0.0f};
    glm::vec2 m_dragAxisScreen{1.0f, 0.0f};
    float m_dragWorldScale = 1.0f;
    TransformComponent m_dragStartTransform{};
    bool m_playing = false;
    bool m_paused = false;
};

} // namespace Demon
