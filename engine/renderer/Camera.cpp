// ==============================================================================
//  DemonEngine::Camera  –  Implementation
// ==============================================================================
#include "Camera.h"

namespace Demon {

Camera::Camera(float fovYDeg, float aspectRatio, float nearClip, float farClip) {
    setPerspective(fovYDeg, aspectRatio, nearClip, farClip);
    recalcView();
}

void Camera::setPerspective(float fovYDeg, float aspectRatio, float nearClip, float farClip) {
    m_projType = ProjectionType::Perspective;
    m_fovY     = fovYDeg;
    m_aspect   = aspectRatio;
    m_nearClip = nearClip;
    m_farClip  = farClip;
    recalcProjection();
}

void Camera::setOrthographic(float size, float nearClip, float farClip) {
    m_projType  = ProjectionType::Orthographic;
    m_orthoSize = size;
    m_nearClip  = nearClip;
    m_farClip   = farClip;
    recalcProjection();
}

void Camera::setAspectRatio(float ratio) {
    m_aspect = ratio;
    recalcProjection();
}

void Camera::setViewportSize(uint32_t width, uint32_t height) {
    if (height == 0) return;
    m_aspect = static_cast<float>(width) / static_cast<float>(height);
    recalcProjection();
}

void Camera::setPosition(const glm::vec3& pos) {
    m_position = pos;
    recalcView();
}

void Camera::setRotation(const glm::vec3& eulerDeg) {
    glm::vec3 rad  = glm::radians(eulerDeg);
    m_pitch        = rad.x;
    m_yaw          = rad.y;
    m_orientation  = glm::quat(rad);
    recalcView();
}

void Camera::lookAt(const glm::vec3& target, const glm::vec3& up) {
    m_view = glm::lookAt(m_position, target, up);
}

void Camera::setTransform(const glm::vec3& pos, const glm::vec3& eulerDeg) {
    m_position    = pos;
    glm::vec3 rad = glm::radians(eulerDeg);
    m_orientation = glm::quat(rad);
    glm::mat4 rotation  = glm::toMat4(m_orientation);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos) * rotation;
    m_view = glm::inverse(transform);
}

void Camera::setFpsTransform(const glm::vec3& pos, float yawDeg, float pitchDeg) {
    m_position = pos;
    m_yaw = glm::radians(yawDeg);
    m_pitch = glm::radians(glm::clamp(pitchDeg, -89.0f, 89.0f));

    const glm::quat yaw = glm::angleAxis(m_yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitch = glm::angleAxis(m_pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    m_orientation = glm::normalize(yaw * pitch);

    const glm::mat4 rotation = glm::toMat4(m_orientation);
    const glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_position) * rotation;
    m_view = glm::inverse(transform);
    m_focalPoint = m_position + getForward() * m_distance;
}

void Camera::setViewMatrix(const glm::mat4& view) {
    m_view        = view;
    glm::mat4 inv = glm::inverse(view);
    m_position    = glm::vec3(inv[3]);
    m_orientation = glm::normalize(glm::quat_cast(inv));
    glm::vec3 euler = glm::eulerAngles(m_orientation);
    m_pitch      = euler.x;
    m_yaw        = euler.y;
    m_focalPoint = m_position + getForward() * m_distance;
}

void Camera::orbitAroundTarget(float deltaYaw, float deltaPitch) {
    m_yaw   += deltaYaw;
    m_pitch += deltaPitch;
    // Clamp to just under ±90° to avoid gimbal flip at the poles
    m_pitch  = glm::clamp(m_pitch, -glm::half_pi<float>() + 0.01f,
                                    glm::half_pi<float>() - 0.01f);
    recalcView();
}

void Camera::pan(float dx, float dy) {
    glm::vec3 right = getRight();
    glm::vec3 up    = getUp();
    m_focalPoint   += (-right * dx + up * dy);
    recalcView();
}

void Camera::zoom(float delta) {
    m_distance = glm::max(0.1f, m_distance - delta);
    recalcView();
}

glm::vec3 Camera::getForward() const {
    return glm::normalize(glm::vec3(m_orientation * glm::vec3(0, 0, -1)));
}

glm::vec3 Camera::getRight() const {
    return glm::normalize(glm::vec3(m_orientation * glm::vec3(1, 0, 0)));
}

glm::vec3 Camera::getUp() const {
    return glm::normalize(glm::vec3(m_orientation * glm::vec3(0, 1, 0)));
}

std::pair<glm::vec3, glm::vec3> Camera::castRay(float ndcX, float ndcY) const {
    glm::mat4 invVP    = glm::inverse(m_projection * m_view);
    glm::vec4 nearPt   = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPt    = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPt            /= nearPt.w;
    farPt             /= farPt.w;
    glm::vec3 origin   = glm::vec3(nearPt);
    glm::vec3 dir      = glm::normalize(glm::vec3(farPt - nearPt));
    return { origin, dir };
}

void Camera::recalcProjection() {
    if (m_projType == ProjectionType::Perspective) {
        m_projection = glm::perspective(glm::radians(m_fovY), m_aspect, m_nearClip, m_farClip);
    } else {
        float half   = m_orthoSize * 0.5f;
        m_projection = glm::ortho(-half * m_aspect, half * m_aspect, -half, half, m_nearClip, m_farClip);
    }
}

void Camera::recalcView() {
    m_orientation = glm::quat(glm::vec3(m_pitch, m_yaw, 0.0f));
    glm::vec3 forward = getForward();
    m_position    = m_focalPoint - forward * m_distance;
    m_view        = glm::lookAt(m_position, m_focalPoint, glm::vec3(0, 1, 0));
}

} // namespace Demon
