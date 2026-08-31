#include "core/Application.h"
#include "core/Layer.h"
#include "core/Logger.h"
#include "core/ProjectSettings.h"
#include "input/Input.h"
#include "scripting/ScriptEngine.h"
#include "scene/Components.h"
#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#ifdef DEMON_PLATFORM_WINDOWS
#   include <shobjidl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace Demon;

namespace {

constexpr float kRuntimeMouseSensitivity = 0.12f;
constexpr float kPlayerEyeHeight = 1.72f;
constexpr float kPlayerRadius = 0.35f;
constexpr float kPlayerHalfHeight = 0.86f;
constexpr float kGroundProbeDistance = 64.0f;

struct RuntimeConfig {
    std::filesystem::path executableDir;
    std::filesystem::path configPath;
    std::filesystem::path startupScene;
    std::string appName = "DemonGame";
    std::string outputName = "DemonGame";
    std::string windowTitle = "DemonGame";
    std::string companyName = "Code Studio Games";
    std::string appVersion = "1.0.0";
};

std::filesystem::path executablePath()
{
#ifdef DEMON_PLATFORM_WINDOWS
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
#endif
    return std::filesystem::current_path() / "DemonGamePlayer.exe";
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string readJsonString(std::string_view json, std::string_view key, std::string fallback = {})
{
    const std::string needle = "\"" + std::string(key) + "\"";
    const size_t keyPos = json.find(needle);
    if (keyPos == std::string_view::npos)
        return fallback;

    const size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string_view::npos)
        return fallback;

    size_t quotePos = json.find('"', colonPos + 1);
    if (quotePos == std::string_view::npos)
        return fallback;

    std::string value;
    bool escaping = false;
    for (size_t i = quotePos + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaping) {
            switch (ch) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(ch); break;
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"')
            return value;
        value.push_back(ch);
    }

    return fallback;
}

bool hasDemonSceneExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    for (char& ch : extension)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return extension == ".demonscene";
}

std::filesystem::path findFirstDemonScene(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> scenes;
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec))
        return {};

    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc)
            continue;
        if (hasDemonSceneExtension(it->path()))
            scenes.push_back(it->path());
    }

    std::sort(scenes.begin(), scenes.end(), [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });
    return scenes.empty() ? std::filesystem::path{} : scenes.front();
}

std::filesystem::path discoverPackagedStartupScene(const std::filesystem::path& executableDir)
{
    const std::filesystem::path dataRoot = executableDir / "Data";
    const std::filesystem::path assetsRoot = executableDir / "assets";
    for (const std::filesystem::path& root : {
             dataRoot / "Scenes",
             assetsRoot / "scenes",
             assetsRoot,
             dataRoot
         })
    {
        if (std::filesystem::path scene = findFirstDemonScene(root); !scene.empty())
            return scene;
    }
    return {};
}

std::filesystem::path makeAbsolute(const std::filesystem::path& base,
                                   const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
        return path;
    return base / path;
}

RuntimeConfig loadRuntimeConfig(int argc, char** argv)
{
    RuntimeConfig config;
    config.executableDir = executablePath().parent_path();
    config.configPath = config.executableDir / "Data" / "runtime_config.json";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--config" && i + 1 < argc)
            config.configPath = argv[++i];
    }

    std::string json = readText(config.configPath);
    if (json.empty()) {
        const std::filesystem::path manifestPath = config.executableDir / "Data" / "build_manifest.json";
        json = readText(manifestPath);
        if (!json.empty())
            config.configPath = manifestPath;
    }

    config.appName = readJsonString(json, "appName", config.appName);
    config.outputName = readJsonString(json, "outputName", config.outputName);
    config.windowTitle = readJsonString(json, "windowTitle",
                                        config.outputName.empty() ? config.appName : config.outputName);
    config.companyName = readJsonString(json, "companyName", config.companyName);
    config.appVersion = readJsonString(json, "appVersion", config.appVersion);

    const std::string startupScene = readJsonString(json, "startupScene", {});
    config.startupScene = makeAbsolute(config.executableDir, startupScene);
    std::error_code startupEc;
    if (config.startupScene.empty() || !std::filesystem::exists(config.startupScene, startupEc))
        config.startupScene = discoverPackagedStartupScene(config.executableDir);
    return config;
}

void ensureFallbackCamera(const std::shared_ptr<Scene>& scene)
{
    if (!scene || scene->getPrimaryCameraID() != NULL_ENTITY)
        return;

    Entity camera = scene->createEntity("Runtime Camera");
    camera.addComponent<TransformComponent>().translation = {0.0f, 2.5f, 6.0f};
    auto& cameraComponent = camera.addComponent<CameraComponent>();
    cameraComponent.primary = true;
}

void ensureRuntimePlayerPhysics(Scene& scene, EntityID cameraId)
{
    if (cameraId == NULL_ENTITY)
        return;

    if (!scene.getComponent<RigidBodyComponent>(cameraId)) {
        RigidBodyComponent body;
        body.type = BodyType::Kinematic;
        body.isKinematic = true;
        body.useGravity = false;
        body.mass = 80.0f;
        body.lockRotation = true;
        scene.addComponent<RigidBodyComponent>(cameraId, body);
    } else if (auto* body = scene.getComponent<RigidBodyComponent>(cameraId)) {
        body->type = BodyType::Kinematic;
        body->isKinematic = true;
        body->simulatePhysics = true;
        body->useGravity = false;
        body->lockRotation = true;
    }

    if (!scene.getComponent<BoxColliderComponent>(cameraId)) {
        BoxColliderComponent collider;
        collider.halfExtents = {kPlayerRadius, kPlayerHalfHeight, kPlayerRadius};
        collider.offset = {0.0f, -kPlayerHalfHeight, 0.0f};
        collider.friction = 0.85f;
        collider.restitution = 0.0f;
        scene.addComponent<BoxColliderComponent>(cameraId, collider);
    }
}

void extractFpsAnglesFromWorldTransform(const glm::mat4& worldTransform,
                                        float& yawDegrees,
                                        float& pitchDegrees)
{
    glm::vec3 forward = glm::vec3(worldTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    const float lengthSq = glm::dot(forward, forward);
    if (lengthSq <= 0.000001f)
        return;

    forward = glm::normalize(forward);
    pitchDegrees = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    yawDegrees = glm::degrees(std::atan2(-forward.x, -forward.z));
}

class GameRuntimeLayer final : public Layer {
public:
    explicit GameRuntimeLayer(RuntimeConfig config)
        : Layer("GameRuntimeLayer"), m_config(std::move(config))
    {
    }

    void onAttach() override
    {
        std::error_code ec;
        if (!m_config.executableDir.empty())
            std::filesystem::current_path(m_config.executableDir, ec);

        m_scene = Scene::create(m_config.outputName.empty() ? m_config.appName : m_config.outputName);
        if (!m_config.startupScene.empty() && std::filesystem::exists(m_config.startupScene)) {
            SceneSerializer serializer(m_scene);
            if (!serializer.deserialize(m_config.startupScene.string()))
                DEMON_LOG_ERROR("GamePlayer: failed to load startup scene '{}'.", m_config.startupScene.string());
        } else {
            DEMON_LOG_ERROR("GamePlayer: startup scene missing '{}'.", m_config.startupScene.string());
        }

        ensureFallbackCamera(m_scene);
        configureRuntimeScripts();
        m_scene->setSimulationEnabled(true);
        m_fallbackCamera.setPerspective(60.0f, 16.0f / 9.0f, 0.05f, 2000.0f);
        m_fallbackCamera.setFpsTransform({0.0f, 2.5f, 6.0f}, 180.0f, -15.0f);
        syncRuntimeCameraFromScene();
        resizeRuntimeViewport(Application::get().getWindow().getWidth(),
                              Application::get().getWindow().getHeight());

        DEMON_LOG_INFO("GamePlayer: running '{}' version {}.", m_config.windowTitle, m_config.appVersion);
    }

    void onDetach() override
    {
        if (m_scene)
            ScriptEngine::get().endRuntime(*m_scene);
        Input::setMouseLocked(false);
    }

    void onUpdate(float dt) override
    {
        if (Input::isKeyPressed(Key::Escape)) {
            if (Input::isMouseLocked())
                Input::setMouseLocked(false);
            else
                Application::get().close();
        }

        const Window& window = Application::get().getWindow();
        if (window.getWidth() != m_lastViewportWidth || window.getHeight() != m_lastViewportHeight)
            resizeRuntimeViewport(window.getWidth(), window.getHeight());

        updateRuntimeCamera(dt);
        if (m_scene)
            m_scene->onUpdate(dt);
    }

    void onRender(Renderer& renderer) override
    {
        if (!m_scene)
            return;

        Camera* activeCamera = &m_fallbackCamera;
        const EntityID cameraId = m_scene->getPrimaryCameraID();
        if (cameraId != NULL_ENTITY) {
            if (auto* camera = m_scene->getComponent<CameraComponent>(cameraId)) {
                camera->camera.setViewportSize(Application::get().getWindow().getWidth(),
                                               Application::get().getWindow().getHeight());
                if (m_cameraControllerInitialized && m_nativePlayerControllerEnabled) {
                    camera->camera.setFpsTransform(m_runtimeCameraPosition,
                                                   m_runtimeCameraYaw,
                                                   m_runtimeCameraPitch);
                } else {
                    camera->camera.setViewMatrix(glm::inverse(m_scene->getWorldTransform(cameraId)));
                }
                activeCamera = &camera->camera;
            }
        }

        renderer.beginScene(*activeCamera);
        m_scene->onRender(renderer);
        renderer.endScene();
    }

    void onEvent(Event& event) override
    {
        EventDispatcher dispatcher(event);
        dispatcher.dispatch<WindowResizeEvent>([this](WindowResizeEvent& resize) {
            resizeRuntimeViewport(resize.width, resize.height);
            return false;
        });
    }

private:
    void configureRuntimeScripts()
    {
        if (!m_scene)
            return;

        ProjectSettings settings;
        settings.applyProjectDefaults(m_config.appName.empty() ? m_config.outputName : m_config.appName);

        const std::filesystem::path assetsRoot = m_config.executableDir / "assets";
        ScriptEngine& scripts = ScriptEngine::get();
        scripts.configureProject(m_config.executableDir, assetsRoot, settings);
        if (!scripts.compileAndHotReload(nullptr, m_scene.get())) {
            DEMON_LOG_ERROR("GamePlayer: DemonScript compile failed for packaged assets '{}'.",
                            assetsRoot.string());
            return;
        }
        scripts.beginRuntime(*m_scene);
    }

    void syncRuntimeCameraFromScene()
    {
        if (!m_scene)
            return;

        const EntityID cameraId = m_scene->getPrimaryCameraID();
        m_runtimeCameraEntity = cameraId;
        if (cameraId != NULL_ENTITY) {
            if (auto* camera = m_scene->getComponent<CameraComponent>(cameraId))
                m_runtimeCamera = camera->camera;

            const glm::mat4 worldTransform = m_scene->getWorldTransform(cameraId);
            m_runtimeCameraPosition = glm::vec3(worldTransform[3]);
            extractFpsAnglesFromWorldTransform(worldTransform, m_runtimeCameraYaw, m_runtimeCameraPitch);
            const auto* script = m_scene->getComponent<ScriptComponent>(cameraId);
            m_nativePlayerControllerEnabled = !script || script->className.empty();
        } else {
            m_runtimeCamera = m_fallbackCamera;
            m_nativePlayerControllerEnabled = true;
        }

        if (m_nativePlayerControllerEnabled && cameraId != NULL_ENTITY) {
            ensureRuntimePlayerPhysics(*m_scene, cameraId);
            snapRuntimeCameraToGround();
        }

        const Window& window = Application::get().getWindow();
        m_runtimeCamera.setViewportSize(window.getWidth(), window.getHeight());
        m_runtimeCamera.setFpsTransform(m_runtimeCameraPosition, m_runtimeCameraYaw, m_runtimeCameraPitch);
        m_cameraControllerInitialized = true;
        if (m_nativePlayerControllerEnabled)
            applyRuntimeCameraToScene();
    }

    void updateRuntimeCamera(float dt)
    {
        if (!m_cameraControllerInitialized)
            syncRuntimeCameraFromScene();
        if (!m_cameraControllerInitialized)
            return;
        if (!m_nativePlayerControllerEnabled)
            return;

        if (Input::isMouseButtonPressed(MouseButton::Left) ||
            Input::isMouseButtonPressed(MouseButton::Right))
        {
            Input::setMouseLocked(true);
        }

        bool changed = false;
        if (Input::isMouseLocked()) {
            auto [dx, dy] = Input::getMouseDelta();
            if (std::abs(dx) > 0.0001f || std::abs(dy) > 0.0001f) {
                m_runtimeCameraYaw -= dx * kRuntimeMouseSensitivity;
                m_runtimeCameraPitch = std::clamp(m_runtimeCameraPitch - dy * kRuntimeMouseSensitivity, -89.0f, 89.0f);
                changed = true;
            }
        }

        m_runtimeCamera.setFpsTransform(m_runtimeCameraPosition,
                                        m_runtimeCameraYaw,
                                        m_runtimeCameraPitch);

        glm::vec3 forward = m_runtimeCamera.getForward();
        forward.y = 0.0f;
        if (glm::length2(forward) > 0.0001f)
            forward = glm::normalize(forward);
        else
            forward = {0.0f, 0.0f, -1.0f};

        glm::vec3 right = m_runtimeCamera.getRight();
        right.y = 0.0f;
        if (glm::length2(right) > 0.0001f)
            right = glm::normalize(right);
        else
            right = {1.0f, 0.0f, 0.0f};

        glm::vec3 movement{0.0f};
        if (Input::isKeyDown(Key::W)) movement += forward;
        if (Input::isKeyDown(Key::S)) movement -= forward;
        if (Input::isKeyDown(Key::D)) movement += right;
        if (Input::isKeyDown(Key::A)) movement -= right;

        if (glm::dot(movement, movement) > 0.0001f) {
            const bool fast = Input::isKeyDown(Key::LeftShift) || Input::isKeyDown(Key::RightShift);
            const float speed = fast ? 9.0f : 4.8f;
            const glm::vec3 velocity = glm::normalize(movement) * speed;
            m_runtimeCameraPosition += velocity * dt;
            if (auto* body = m_scene ? m_scene->getComponent<RigidBodyComponent>(m_runtimeCameraEntity) : nullptr)
                body->linearVelocity = {velocity.x, 0.0f, velocity.z};
            changed = true;
        } else if (auto* body = m_scene ? m_scene->getComponent<RigidBodyComponent>(m_runtimeCameraEntity) : nullptr) {
            body->linearVelocity = {0.0f, 0.0f, 0.0f};
        }

        if (snapRuntimeCameraToGround())
            changed = true;

        if (changed)
            applyRuntimeCameraToScene();
    }

    bool snapRuntimeCameraToGround()
    {
        if (!m_scene)
            return false;

        glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
        const float groundY = m_scene->sampleGroundAtWorld(m_runtimeCameraPosition.x,
                                                           m_runtimeCameraPosition.z,
                                                           m_runtimeCameraPosition.y + kPlayerEyeHeight,
                                                           kGroundProbeDistance,
                                                           &groundNormal);
        if (groundY <= std::numeric_limits<float>::lowest())
            return false;

        const float snappedY = groundY + kPlayerEyeHeight;
        if (std::abs(m_runtimeCameraPosition.y - snappedY) <= 0.0001f)
            return false;

        m_runtimeCameraPosition.y = snappedY;
        return true;
    }

    void applyRuntimeCameraToScene()
    {
        m_runtimeCamera.setFpsTransform(m_runtimeCameraPosition,
                                        m_runtimeCameraYaw,
                                        m_runtimeCameraPitch);

        if (!m_scene)
            return;

        const EntityID cameraId = m_scene->getPrimaryCameraID();
        if (cameraId == NULL_ENTITY)
            return;

        if (auto* transform = m_scene->getComponent<TransformComponent>(cameraId)) {
            transform->translation = m_runtimeCameraPosition;
            transform->rotation = {m_runtimeCameraPitch, m_runtimeCameraYaw, 0.0f};
        }
        if (auto* camera = m_scene->getComponent<CameraComponent>(cameraId)) {
            const Window& window = Application::get().getWindow();
            camera->camera.setViewportSize(window.getWidth(), window.getHeight());
            camera->camera.setFpsTransform(m_runtimeCameraPosition,
                                           m_runtimeCameraYaw,
                                           m_runtimeCameraPitch);
        }
    }

    void resizeRuntimeViewport(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return;
        if (m_scene)
            m_scene->onViewportResize(width, height);
        m_fallbackCamera.setViewportSize(width, height);
        m_runtimeCamera.setViewportSize(width, height);
        Application::get().getRenderer().resizeViewport(width, height);
        m_lastViewportWidth = width;
        m_lastViewportHeight = height;
    }

    RuntimeConfig m_config;
    std::shared_ptr<Scene> m_scene;
    Camera m_fallbackCamera;
    Camera m_runtimeCamera;
    glm::vec3 m_runtimeCameraPosition{0.0f, 2.5f, 6.0f};
    float m_runtimeCameraYaw = 0.0f;
    float m_runtimeCameraPitch = -15.0f;
    uint32_t m_lastViewportWidth = 0;
    uint32_t m_lastViewportHeight = 0;
    EntityID m_runtimeCameraEntity = NULL_ENTITY;
    bool m_cameraControllerInitialized = false;
    bool m_nativePlayerControllerEnabled = true;
};

class DemonGamePlayerApp final : public Application {
public:
    explicit DemonGamePlayerApp(RuntimeConfig config)
        : Application({
            .name = config.windowTitle.empty() ? "DemonGame" : config.windowTitle,
            .width = 1280,
            .height = 720,
            .vsync = true,
            .resizable = true
        })
    {
        pushLayer(std::make_unique<GameRuntimeLayer>(std::move(config)));
    }
};

} // namespace

int main(int argc, char** argv)
{
#ifdef DEMON_PLATFORM_WINDOWS
    SetCurrentProcessExplicitAppUserModelID(L"CodeStudioGames.DemonEngine.GamePlayer");
#endif

    RuntimeConfig config = loadRuntimeConfig(argc, argv);
    if (!config.executableDir.empty()) {
        std::error_code ec;
        std::filesystem::current_path(config.executableDir, ec);
    }

    DemonGamePlayerApp app(std::move(config));
    app.run();
    return 0;
}
