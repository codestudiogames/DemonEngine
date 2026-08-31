#pragma once
// ==============================================================================
//  DemonEngine::Camera
//  Perspective / orthographic camera with GLM matrices.
//  Used by the renderer and editor viewport.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

enum class ProjectionType { Perspective = 0, Orthographic };

class Camera {
public:
    Camera() = default;
    Camera(float fovYDeg, float aspectRatio, float nearClip, float farClip);

    // ── Projection ────────────────────────────────────────────────────────────
    void setPerspective(float fovYDeg, float aspectRatio, float nearClip, float farClip);
    void setOrthographic(float size, float nearClip, float farClip);
    void setAspectRatio(float ratio);
    void setViewportSize(uint32_t width, uint32_t height);

    // ── View ──────────────────────────────────────────────────────────────────
    void setPosition(const glm::vec3& pos);
    void setRotation(const glm::vec3& eulerDeg);
    void lookAt(const glm::vec3& target, const glm::vec3& up = {0, 1, 0});
    void setTransform(const glm::vec3& pos, const glm::vec3& eulerDeg);
    void setFpsTransform(const glm::vec3& pos, float yawDeg, float pitchDeg);
    void setViewMatrix(const glm::mat4& view);

    // ── Editor orbit controls ─────────────────────────────────────────────────
    void orbitAroundTarget(float deltaYaw, float deltaPitch);
    void pan(float dx, float dy);
    void zoom(float delta);

    void setFocalPoint(const glm::vec3& pt) { m_focalPoint = pt; recalcView(); }
    void setDistance(float d)               { m_distance   = d;  recalcView(); }

    // ── Getters ───────────────────────────────────────────────────────────────
    [[nodiscard]] const glm::mat4& getViewMatrix()       const { return m_view; }
    [[nodiscard]] const glm::mat4& getProjectionMatrix() const { return m_projection; }
    [[nodiscard]] glm::mat4        getViewProjection()   const { return m_projection * m_view; }

    [[nodiscard]] glm::vec3 getPosition() const { return m_position; }
    [[nodiscard]] glm::vec3 getForward()  const;
    [[nodiscard]] glm::vec3 getRight()    const;
    [[nodiscard]] glm::vec3 getUp()       const;

    [[nodiscard]] float          getFovY()           const { return m_fovY; }
    [[nodiscard]] float          getNearClip()       const { return m_nearClip; }
    [[nodiscard]] float          getFarClip()        const { return m_farClip; }
    [[nodiscard]] float          getAspect()         const { return m_aspect; }
    [[nodiscard]] float          getDistance()       const { return m_distance; }
    [[nodiscard]] ProjectionType getProjectionType() const { return m_projType; }

    // Returns world-space { origin, direction } from screen NDC [-1, 1]
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> castRay(float ndcX, float ndcY) const;

private:
    void recalcProjection();
    void recalcView();

    ProjectionType m_projType  = ProjectionType::Perspective;
    glm::mat4      m_projection{1.0f};
    glm::mat4      m_view      {1.0f};
    glm::vec3      m_position  {0, 0, 5};
    glm::vec3      m_focalPoint{0, 0, 0};
    glm::quat      m_orientation{1, 0, 0, 0};

    float m_fovY      = 45.0f;
    float m_aspect    = 16.0f / 9.0f;
    float m_nearClip  = 0.1f;
    float m_farClip   = 1000.0f;
    float m_orthoSize = 10.0f;
    float m_distance  = 5.0f;
    float m_yaw       = 0.0f;
    float m_pitch     = 0.0f;
};

} // namespace Demon
