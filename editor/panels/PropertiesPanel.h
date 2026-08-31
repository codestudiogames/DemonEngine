#pragma once
// ==============================================================================
//  DemonEngine Editor::PropertiesPanel
//  Displays and edits the components of the currently selected entity.
// ==============================================================================
#include "../engine/core/DemonPCH.h"
#include "scene/Scene.h"
#include "scene/Components.h"

namespace Demon {

class PropertiesPanel {
public:
    void render(Entity selectedEntity);
    bool consumeSceneEdited() { bool v = m_sceneEdited; m_sceneEdited = false; return v; }
    void setScene(Scene* scene) { m_scene = scene; }
    void setAssetsRoot(const std::filesystem::path& root) { m_assetsRoot = root; }
    void setMaterialEditor(class MaterialEditorPanel* editor) { m_materialEditor = editor; }

private:
    void drawTagComponent(Entity e);
    void drawTransformComponent(Entity e);
    void drawMeshRendererComponent(Entity e);
    void drawAnimatorComponent(Entity e);
    void drawMaterialComponent(Entity e);
    void drawCameraComponent(Entity e);
    void drawLightComponent(Entity e);
    void drawSkyboxComponent(Entity e);
    void drawFogComponent(Entity e);
    void drawVolumetricFogComponent(Entity e);
    void drawLocalVolumetricFogComponent(Entity e);
    void drawVolumetricCloudComponent(Entity e);
    void drawLensFlareComponent(Entity e);
    void drawReflectionProbeComponent(Entity e);
    void drawIrradianceProbeVolumeComponent(Entity e);
    void drawRigidBodyComponent(Entity e);
    void drawBoxColliderComponent(Entity e);
    void drawTerrainComponent(Entity e);
    void drawTerrainSculptComponent(Entity e);
    void drawTerrainFoliageComponent(Entity e);
    void drawWaterBodyComponent(Entity e);
    void drawScriptComponent(Entity e);
    void drawUIElementComponent(Entity e);

    template<typename T>
    void drawComponent(const char* label, Entity e, std::function<void(T&)> uiFn);

    void drawAddComponentButton(Entity e);
    void markEdited() { m_sceneEdited = true; }

    bool m_sceneEdited = false;
    Scene* m_scene = nullptr;
    std::filesystem::path m_assetsRoot;
    MaterialEditorPanel* m_materialEditor = nullptr;
};

} // namespace Demon
