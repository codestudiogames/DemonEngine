#include "ImGuiEditorLayer.h"
#include "core/Application.h"
#include "scene/Components.h"
#include "scene/Scene.h"
#include "../ProjectHubWindow/ProjectHubWindow.h"

#include <filesystem>
#include <optional>

using namespace Demon;

namespace {

std::shared_ptr<Scene> createDefaultScene()
{
    auto scene = Scene::create("Untitled Scene");

    auto camera = scene->createEntity("Camera");
    camera.addComponent<TransformComponent>().translation = {0.0f, 2.5f, 6.0f};
    camera.addComponent<CameraComponent>();

    auto light = scene->createEntity("Directional Light");
    light.addComponent<TransformComponent>().rotation = {-45.0f, 45.0f, 0.0f};
    auto& lightComponent = light.addComponent<LightComponent>();
    lightComponent.type = LightType::Directional;
    lightComponent.intensity = 2.0f;

    auto ground = scene->createEntity("Ground");
    ground.addComponent<TransformComponent>();
    ground.addComponent<MeshRendererComponent>().meshPath = "builtin:plane";
    ground.addComponent<MaterialComponent>().roughness = 0.95f;

    auto cube = scene->createEntity("Cube");
    cube.addComponent<TransformComponent>().translation = {0.0f, 0.5f, 0.0f};
    cube.addComponent<MeshRendererComponent>().meshPath = "builtin:cube";
    cube.addComponent<MaterialComponent>();
    return scene;
}

std::optional<ProjectHub::Result> parseProjectArgument(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if ((arg == "--project" || arg == "-p") && i + 1 < argc) {
            std::filesystem::path configPath = argv[++i];
            if (!configPath.empty() && std::filesystem::exists(configPath)) {
                ProjectHub::Result result;
                result.action = ProjectHub::Action::Open;
                result.configPath = std::filesystem::absolute(configPath);
                result.projectDir = result.configPath.parent_path();
                result.name = result.configPath.stem().string();
                return result;
            }
        }
    }
    return std::nullopt;
}

class DemonEditorApp final : public Application {
public:
    explicit DemonEditorApp(const ProjectHub::Result& project)
        : Application({
            .name = "DemonEngine Editor v1.2",
            .width = 1600,
            .height = 900,
            .vsync = true,
            .resizable = true
        })
    {
        auto editor = std::make_unique<ImGuiEditorLayer>();
        editor->setProjectContext(project.name.empty() ? "Untitled Project" : project.name,
                                  project.projectDir,
                                  project.configPath);
        editor->setScene(createDefaultScene(), false);
        pushOverlay(std::move(editor));
    }
};

} // namespace

int main(int argc, char** argv)
{
    auto project = parseProjectArgument(argc, argv);
    if (!project)
        project = ProjectHub::show();
    if (!project)
        return 0;

    DemonEditorApp app(*project);
    app.run();
    return 0;
}
