#include "EditorRuntimeLayer.h"

#include "core/Application.h"
#include "input/Input.h"
#include "renderer/Renderer.h"

namespace Demon {
namespace {

bool projectPoint(const Camera& camera, const glm::vec3& world, glm::vec2& ndc)
{
    const glm::vec4 clip = camera.getViewProjection() * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f)
        return false;
    ndc = glm::vec2(clip) / clip.w;
    return true;
}

float pointSegmentDistance(const glm::vec2& point, const glm::vec2& start, const glm::vec2& end)
{
    const glm::vec2 segment = end - start;
    const float lengthSquared = glm::dot(segment, segment);
    if (lengthSquared <= 1e-8f)
        return glm::length(point - start);
    const float t = std::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
    return glm::length(point - (start + segment * t));
}

float snapValue(float value, float step)
{
    return step > 0.0f ? std::round(value / step) * step : value;
}

glm::mat4 segmentTransform(const glm::vec3& center, const glm::vec3& direction,
                           float length, float thickness)
{
    const glm::vec3 x = glm::normalize(direction);
    const glm::vec3 reference = std::abs(x.y) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::vec3 z = glm::normalize(glm::cross(x, reference));
    const glm::vec3 y = glm::normalize(glm::cross(z, x));
    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(x, 0.0f);
    basis[1] = glm::vec4(y, 0.0f);
    basis[2] = glm::vec4(z, 0.0f);
    return glm::translate(glm::mat4(1.0f), center) * basis *
           glm::scale(glm::mat4(1.0f), {length, thickness, thickness});
}

glm::vec3 rotationRingPoint(const glm::vec3& origin, int axis, float angle, float radius)
{
    if (axis == 0)
        return origin + glm::vec3(0.0f, std::cos(angle), std::sin(angle)) * radius;
    if (axis == 1)
        return origin + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
    return origin + glm::vec3(std::cos(angle), std::sin(angle), 0.0f) * radius;
}

} // namespace

EditorRuntimeLayer::EditorRuntimeLayer(std::shared_ptr<Scene> scene)
    : Layer("QtEditorRuntime"), m_scene(std::move(scene))
{
    m_editorCamera.setFpsTransform(m_cameraPosition, m_cameraYaw, m_cameraPitch);
    m_editorCube = Mesh::createCube(1.0f);

    m_gridMinorMaterial.setAlbedo({0.24f, 0.27f, 0.32f, 0.58f});
    m_gridMinorMaterial.setEmissive({0.14f, 0.17f, 0.22f}, 0.48f);
    m_gridMinorMaterial.setRoughness(1.0f);
    m_gridMinorMaterial.setAlphaBlend(true);
    m_gridMajorMaterial.setAlbedo({0.42f, 0.47f, 0.56f, 0.78f});
    m_gridMajorMaterial.setEmissive({0.25f, 0.31f, 0.40f}, 0.62f);
    m_gridMajorMaterial.setRoughness(1.0f);
    m_gridMajorMaterial.setAlphaBlend(true);

    m_gizmoXMaterial.setAlbedo({0.92f, 0.12f, 0.10f, 1.0f});
    m_gizmoXMaterial.setEmissive({1.0f, 0.04f, 0.02f}, 0.8f);
    m_gizmoYMaterial.setAlbedo({0.18f, 0.82f, 0.20f, 1.0f});
    m_gizmoYMaterial.setEmissive({0.05f, 1.0f, 0.08f}, 0.8f);
    m_gizmoZMaterial.setAlbedo({0.12f, 0.36f, 0.96f, 1.0f});
    m_gizmoZMaterial.setEmissive({0.03f, 0.18f, 1.0f}, 0.8f);
}

EntityID EditorRuntimeLayer::pickEntity(float ndcX, float ndcY) const
{
    if (!m_scene || m_playing)
        return NULL_ENTITY;
    const auto [origin, direction] = m_editorCamera.castRay(ndcX, ndcY);
    return m_scene->pickEntity(origin, direction);
}

bool EditorRuntimeLayer::beginTransformDrag(float ndcX, float ndcY)
{
    if (!m_scene || m_playing || m_selectedEntity == NULL_ENTITY ||
        !m_scene->entityExists(m_selectedEntity))
        return false;

    auto* transform = m_scene->getComponent<TransformComponent>(m_selectedEntity);
    if (!transform)
        return false;

    const glm::vec3 origin = glm::vec3(m_scene->getWorldTransform(m_selectedEntity)[3]);
    const float distance = glm::distance(origin, m_cameraPosition);
    const float size = std::clamp(distance * 0.12f, 0.55f, 3.0f);
    const glm::vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const glm::vec2 pointer{ndcX, ndcY};
    float bestDistance = 0.065f;
    int bestAxis = -1;
    glm::vec2 bestScreenAxis{};

    if (m_transformTool == TransformTool::Rotate) {
        constexpr int segments = 48;
        for (int axis = 0; axis < 3; ++axis) {
            for (int segment = 0; segment < segments; ++segment) {
                const float a0 = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(segments);
                const float a1 = glm::two_pi<float>() * static_cast<float>(segment + 1) / static_cast<float>(segments);
                glm::vec2 startNdc{}, endNdc{};
                if (!projectPoint(m_editorCamera, rotationRingPoint(origin, axis, a0, size), startNdc) ||
                    !projectPoint(m_editorCamera, rotationRingPoint(origin, axis, a1, size), endNdc))
                    continue;
                const float hitDistance = pointSegmentDistance(pointer, startNdc, endNdc);
                const glm::vec2 screenAxis = endNdc - startNdc;
                if (hitDistance < bestDistance && glm::length2(screenAxis) > 1e-8f) {
                    bestDistance = hitDistance;
                    bestAxis = axis;
                    bestScreenAxis = glm::normalize(screenAxis);
                }
            }
        }
    } else {
        glm::vec2 originNdc{};
        if (!projectPoint(m_editorCamera, origin, originNdc))
            return false;
        for (int axis = 0; axis < 3; ++axis) {
            glm::vec2 endNdc{};
            if (!projectPoint(m_editorCamera, origin + axes[axis] * size, endNdc))
                continue;
            const float hitDistance = pointSegmentDistance(pointer, originNdc, endNdc);
            const glm::vec2 screenAxis = endNdc - originNdc;
            if (hitDistance < bestDistance && glm::length2(screenAxis) > 1e-8f) {
                bestDistance = hitDistance;
                bestAxis = axis;
                bestScreenAxis = glm::normalize(screenAxis);
            }
        }
    }

    if (bestAxis < 0)
        return false;

    m_activeAxis = bestAxis;
    m_dragStartNdc = pointer;
    m_dragAxisScreen = bestScreenAxis;
    m_dragWorldScale = std::max(distance * 0.85f, 1.0f);
    m_dragStartTransform = *transform;
    m_transformDragging = true;
    return true;
}

bool EditorRuntimeLayer::updateTransformDrag(float ndcX, float ndcY)
{
    if (!m_transformDragging || !m_scene || m_activeAxis < 0 ||
        m_selectedEntity == NULL_ENTITY)
        return false;

    auto* transform = m_scene->getComponent<TransformComponent>(m_selectedEntity);
    if (!transform)
        return false;

    const float projectedDelta = glm::dot(glm::vec2(ndcX, ndcY) - m_dragStartNdc, m_dragAxisScreen);
    if (m_transformTool == TransformTool::Translate) {
        float amount = projectedDelta * m_dragWorldScale;
        if (m_snapEnabled)
            amount = snapValue(amount, 0.5f);
        transform->translation = m_dragStartTransform.translation;
        transform->translation[m_activeAxis] += amount;
    } else if (m_transformTool == TransformTool::Rotate) {
        float amount = projectedDelta * 180.0f;
        if (m_snapEnabled)
            amount = snapValue(amount, 15.0f);
        transform->rotation = m_dragStartTransform.rotation;
        transform->rotation[m_activeAxis] += amount;
    } else {
        float amount = projectedDelta * 2.0f;
        if (m_snapEnabled)
            amount = snapValue(amount, 0.1f);
        transform->scale = m_dragStartTransform.scale;
        transform->scale[m_activeAxis] = std::max(0.001f, m_dragStartTransform.scale[m_activeAxis] + amount);
    }
    return true;
}

void EditorRuntimeLayer::endTransformDrag()
{
    m_transformDragging = false;
    m_activeAxis = -1;
}

void EditorRuntimeLayer::focusSelected()
{
    if (!m_scene || m_selectedEntity == NULL_ENTITY || !m_scene->entityExists(m_selectedEntity))
        return;
    const glm::vec3 target = glm::vec3(m_scene->getWorldTransform(m_selectedEntity)[3]);
    m_cameraPosition = target - m_editorCamera.getForward() * 6.0f;
    m_editorCamera.setFpsTransform(m_cameraPosition, m_cameraYaw, m_cameraPitch);
}

void EditorRuntimeLayer::setScene(std::shared_ptr<Scene> scene)
{
    m_scene = std::move(scene);
    m_editScene.reset();
    m_playing = false;
    m_paused = false;
    m_selectedEntity = NULL_ENTITY;
}

void EditorRuntimeLayer::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;

    if (playing) {
        m_editScene = m_scene;
        m_scene = Scene::copy(m_scene);
        if (m_scene)
            m_scene->setSimulationEnabled(true);
    } else if (m_editScene) {
        m_scene = std::move(m_editScene);
        m_scene->setSimulationEnabled(false);
    }

    m_playing = playing;
    m_paused = false;
}

void EditorRuntimeLayer::updateEditorCamera(float dt)
{
    if (m_playing || !Input::isMouseButtonDown(MouseButton::Right))
        return;

    auto [dx, dy] = Input::getMouseDelta();
    m_cameraYaw += dx * 0.12f;
    m_cameraPitch = std::clamp(m_cameraPitch - dy * 0.12f, -89.0f, 89.0f);
    m_editorCamera.setFpsTransform(m_cameraPosition, m_cameraYaw, m_cameraPitch);

    glm::vec3 movement{0.0f};
    if (Input::isKeyDown(Key::W)) movement += m_editorCamera.getForward();
    if (Input::isKeyDown(Key::S)) movement -= m_editorCamera.getForward();
    if (Input::isKeyDown(Key::D)) movement += m_editorCamera.getRight();
    if (Input::isKeyDown(Key::A)) movement -= m_editorCamera.getRight();
    if (Input::isKeyDown(Key::E)) movement += glm::vec3(0.0f, 1.0f, 0.0f);
    if (Input::isKeyDown(Key::Q)) movement -= glm::vec3(0.0f, 1.0f, 0.0f);

    if (glm::length2(movement) > 0.0001f) {
        const float speed = Input::isKeyDown(Key::LeftShift) ? 18.0f : 7.0f;
        m_cameraPosition += glm::normalize(movement) * speed * dt;
        m_editorCamera.setFpsTransform(m_cameraPosition, m_cameraYaw, m_cameraPitch);
    }
}

void EditorRuntimeLayer::onUpdate(float dt)
{
    updateEditorCamera(dt);
    if (m_scene && (!m_playing || !m_paused))
        m_scene->onUpdate(dt);
}

void EditorRuntimeLayer::onRender(Renderer& renderer)
{
    if (!m_scene)
        return;

    const uint32_t width = Application::get().getWindow().getWidth();
    const uint32_t height = Application::get().getWindow().getHeight();
    if (width == 0 || height == 0)
        return;

    if (width != m_lastWidth || height != m_lastHeight) {
        renderer.resizeViewport(width, height);
        m_editorCamera.setViewportSize(width, height);
        m_scene->onViewportResize(width, height);
        m_lastWidth = width;
        m_lastHeight = height;
    }

    Camera* activeCamera = &m_editorCamera;
    if (m_playing) {
        const EntityID cameraId = m_scene->getPrimaryCameraID();
        if (cameraId != NULL_ENTITY) {
            if (auto* camera = m_scene->getComponent<CameraComponent>(cameraId)) {
                if (const auto* transform = m_scene->getComponent<TransformComponent>(cameraId))
                    camera->camera.setFpsTransform(transform->translation, transform->rotation.y, transform->rotation.x);
                activeCamera = &camera->camera;
            }
        }
    }

    m_scene->setSelectedEntity(m_playing ? NULL_ENTITY : m_selectedEntity);
    renderer.beginScene(*activeCamera);
    m_scene->onRender(renderer);
    if (!m_playing) {
        renderEditorGrid(renderer);
        renderTransformGizmo(renderer);
    }
    renderer.endScene();
}

void EditorRuntimeLayer::renderEditorGrid(Renderer& renderer)
{
    if (!m_gridVisible || !m_editorCube)
        return;

    constexpr int halfLines = 55;
    const float cameraHeight = std::max(std::abs(m_cameraPosition.y), 1.0f);
    const float spacing = std::max(1.0f,
        std::pow(10.0f, std::floor(std::log10(cameraHeight)) - 1.0f));
    const float thickness = std::max(spacing * 0.014f, 0.008f);
    const float extent = static_cast<float>(halfLines) * spacing;
    const float centerX = std::floor(m_cameraPosition.x / spacing) * spacing;
    const float centerZ = std::floor(m_cameraPosition.z / spacing) * spacing;

    for (int line = -halfLines; line <= halfLines; ++line) {
        const bool major = (line % 10) == 0;
        Material& material = major ? m_gridMajorMaterial : m_gridMinorMaterial;
        const float offset = static_cast<float>(line) * spacing;

        const glm::mat4 alongX = glm::translate(glm::mat4(1.0f), {centerX, 0.035f, centerZ + offset}) *
                                 glm::scale(glm::mat4(1.0f), {extent * 2.0f, thickness, thickness});
        const glm::mat4 alongZ = glm::translate(glm::mat4(1.0f), {centerX + offset, 0.035f, centerZ}) *
                                 glm::scale(glm::mat4(1.0f), {thickness, thickness, extent * 2.0f});
        renderer.submit(*m_editorCube, material, alongX, nullptr, false, 0, -1, false);
        renderer.submit(*m_editorCube, material, alongZ, nullptr, false, 0, -1, false);
    }
}

void EditorRuntimeLayer::renderTransformGizmo(Renderer& renderer)
{
    if (!m_editorCube || !m_scene || m_selectedEntity == NULL_ENTITY ||
        !m_scene->entityExists(m_selectedEntity) ||
        !m_scene->hasComponent<TransformComponent>(m_selectedEntity))
        return;

    const glm::vec3 origin = glm::vec3(m_scene->getWorldTransform(m_selectedEntity)[3]);
    const float distance = glm::distance(origin, m_cameraPosition);
    const float size = std::clamp(distance * 0.12f, 0.55f, 3.0f);
    const float thickness = size * 0.045f;
    Material* materials[] = {&m_gizmoXMaterial, &m_gizmoYMaterial, &m_gizmoZMaterial};
    const glm::vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    if (m_transformTool == TransformTool::Rotate) {
        constexpr int segments = 48;
        for (int axis = 0; axis < 3; ++axis) {
            for (int segment = 0; segment < segments; ++segment) {
                const float a0 = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(segments);
                const float a1 = glm::two_pi<float>() * static_cast<float>(segment + 1) / static_cast<float>(segments);
                const glm::vec3 p0 = rotationRingPoint(origin, axis, a0, size);
                const glm::vec3 p1 = rotationRingPoint(origin, axis, a1, size);
                renderer.submit(*m_editorCube, *materials[axis],
                    segmentTransform((p0 + p1) * 0.5f, p1 - p0, glm::length(p1 - p0), thickness),
                    nullptr, false, 0, -1, false, true);
            }
        }
        return;
    }

    for (int axis = 0; axis < 3; ++axis) {
        renderer.submit(*m_editorCube, *materials[axis],
            segmentTransform(origin + axes[axis] * (size * 0.5f), axes[axis], size, thickness),
            nullptr, false, 0, -1, false, true);

        const float handleSize = m_transformTool == TransformTool::Scale ? size * 0.16f : size * 0.12f;
        const glm::mat4 handle = glm::translate(glm::mat4(1.0f), origin + axes[axis] * size) *
                                 glm::scale(glm::mat4(1.0f), glm::vec3(handleSize));
        renderer.submit(*m_editorCube, *materials[axis], handle,
                        nullptr, false, 0, -1, false, true);
    }
}

} // namespace Demon
