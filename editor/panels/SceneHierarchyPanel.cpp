// ==============================================================================
//  DemonEngine Editor::SceneHierarchyPanel  -  Implementation
// ==============================================================================
#include "SceneHierarchyPanel.h"

#include <imgui.h>

#include "core/Application.h"
#include "scene/Components.h"
#include "../EditorSettings.h"
#include "../utils/TextBuffer.h"

namespace Demon {

namespace {

enum HierarchyMenuCommand : UINT {
    IdContextCreateEmpty = 0x5001,
    IdContextCreateMesh,
    IdContextCreateCamera,
    IdContextCreateLightDirectional,
    IdContextCreateLightPoint,
    IdContextCreateLightSpot,
    IdContextCreateLandscapeTerrain,
    IdContextCreateFluidLake,
    IdContextCreateFluidRiver,
    IdContextCreateFluidOcean,
    IdContextCreateFluidPool,
    IdContextCreateFluidCustomArea,
    IdContextCreateReflectionProbe,
    IdContextCreateIrradianceProbeVolume,
    IdContextCreateVolumetricFog,
    IdContextCreateLocalVolumetricFog,
    IdContextCreateVolumetricClouds,
    IdContextCreateLensFlare,
    IdContextCreateUIText,
    IdContextCreateUIImage,
    IdContextCreateUIShape,
    IdContextCreateUISprite,
    IdContextCreateUI3DText,
    IdContextCreateUI3DImage,
    IdContextDuplicate,
    IdContextRename,
    IdContextUnparent,
    IdContextDeleteChildren,
    IdContextDelete,
};

UINT showPopupMenu(HMENU menu)
{
    POINT cursor{};
    GetCursorPos(&cursor);
    return static_cast<UINT>(TrackPopupMenu(menu,
                                            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                            cursor.x,
                                            cursor.y,
                                            0,
                                            Application::get().getWindow().getWin32Handle(),
                                            nullptr));
}

HMENU createLightMenu()
{
    HMENU lightMenu = CreatePopupMenu();
    AppendMenuA(lightMenu, MF_STRING, IdContextCreateLightDirectional, "Directional");
    AppendMenuA(lightMenu, MF_STRING, IdContextCreateLightPoint, "Point");
    AppendMenuA(lightMenu, MF_STRING, IdContextCreateLightSpot, "Spot");
    return lightMenu;
}

HMENU createLandscapeMenu()
{
    HMENU landscapeMenu = CreatePopupMenu();
    AppendMenuA(landscapeMenu, MF_STRING, IdContextCreateLandscapeTerrain, "Terrain");
    return landscapeMenu;
}

HMENU createFluidMenu()
{
    HMENU fluidMenu = CreatePopupMenu();
    AppendMenuA(fluidMenu, MF_STRING, IdContextCreateFluidLake, "Lake");
    AppendMenuA(fluidMenu, MF_STRING, IdContextCreateFluidRiver, "River");
    AppendMenuA(fluidMenu, MF_STRING, IdContextCreateFluidOcean, "Ocean");
    AppendMenuA(fluidMenu, MF_STRING, IdContextCreateFluidPool, "Pool");
    AppendMenuA(fluidMenu, MF_STRING, IdContextCreateFluidCustomArea, "Custom Area");
    return fluidMenu;
}

HMENU createEnvironmentMenu()
{
    HMENU environmentMenu = CreatePopupMenu();
    AppendMenuA(environmentMenu, MF_STRING, IdContextCreateReflectionProbe, "Reflection Probe");
    AppendMenuA(environmentMenu, MF_STRING, IdContextCreateIrradianceProbeVolume, "Irradiance Probe Volume");
    AppendMenuA(environmentMenu, MF_STRING, IdContextCreateVolumetricFog, "Volumetric Fog");
    AppendMenuA(environmentMenu, MF_STRING, IdContextCreateLocalVolumetricFog, "Local Fog Volume");
    AppendMenuA(environmentMenu, MF_STRING, IdContextCreateVolumetricClouds, "Volumetric Clouds");
    AppendMenuA(environmentMenu, MF_STRING, IdContextCreateLensFlare, "Lens Flare");
    return environmentMenu;
}

HMENU createUIMenu()
{
    HMENU uiMenu = CreatePopupMenu();
    AppendMenuA(uiMenu, MF_STRING, IdContextCreateUIText, "Text");
    AppendMenuA(uiMenu, MF_STRING, IdContextCreateUIImage, "Image");
    AppendMenuA(uiMenu, MF_STRING, IdContextCreateUIShape, "Shapes (2D)");
    AppendMenuA(uiMenu, MF_STRING, IdContextCreateUISprite, "Sprite");
    return uiMenu;
}

HMENU createUI3DMenu()
{
    HMENU ui3DMenu = CreatePopupMenu();
    AppendMenuA(ui3DMenu, MF_STRING, IdContextCreateUI3DText, "Text 3D");
    AppendMenuA(ui3DMenu, MF_STRING, IdContextCreateUI3DImage, "3D Image");
    return ui3DMenu;
}

HMENU createEntityMenu(const char* title)
{
    HMENU lightMenu = createLightMenu();
    HMENU landscapeMenu = createLandscapeMenu();
    HMENU fluidMenu = createFluidMenu();
    HMENU environmentMenu = createEnvironmentMenu();
    HMENU uiMenu = createUIMenu();
    HMENU ui3DMenu = createUI3DMenu();
    HMENU createMenu = CreatePopupMenu();
    AppendMenuA(createMenu, MF_STRING, IdContextCreateEmpty, "Empty Entity");
    AppendMenuA(createMenu, MF_STRING, IdContextCreateMesh, "Mesh");
    AppendMenuA(createMenu, MF_STRING, IdContextCreateCamera, "Camera");
    AppendMenuA(createMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(uiMenu), "UI");
    AppendMenuA(createMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(ui3DMenu), "3D UI");
    AppendMenuA(createMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(lightMenu), "Light");
    AppendMenuA(createMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(environmentMenu), "Environment");
    AppendMenuA(createMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(landscapeMenu), "Landscape");
    AppendMenuA(createMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(fluidMenu), "Fluid");

    HMENU rootMenu = CreatePopupMenu();
    AppendMenuA(rootMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(createMenu), title);
    return rootMenu;
}

} // namespace

SceneHierarchyPanel::SceneHierarchyPanel(std::shared_ptr<Scene> scene)
    : m_scene(std::move(scene))
{
}

void SceneHierarchyPanel::setScene(std::shared_ptr<Scene> scene)
{
    if (m_scene == scene)
        return;

    m_scene = std::move(scene);
    m_selected = {};
    m_selectedIds.clear();
}

std::vector<EntityID> SceneHierarchyPanel::getSelectedEntityIDs() const
{
    std::vector<EntityID> ids;
    ids.reserve(m_selectedIds.size());
    for (EntityID id : m_selectedIds) {
        if (!m_scene || m_scene->entityExists(id))
            ids.push_back(id);
    }
    if (ids.empty() && m_selected)
        ids.push_back(m_selected.getID());
    return ids;
}

void SceneHierarchyPanel::setSelectedEntity(Entity e)
{
    m_selected = e;
    m_selectedIds.clear();
    if (e)
        m_selectedIds.insert(e.getID());
}

void SceneHierarchyPanel::selectAllEntities()
{
    m_selected = {};
    m_selectedIds.clear();
    if (!m_scene)
        return;
    for (EntityID id : m_scene->getEntities())
        m_selectedIds.insert(id);
    if (!m_selectedIds.empty())
        m_selected = m_scene->getEntityByID(*m_selectedIds.begin());
}

void SceneHierarchyPanel::toggleSelectedEntity(Entity e)
{
    if (!e)
        return;

    const EntityID id = e.getID();
    if (m_selectedIds.contains(id)) {
        m_selectedIds.erase(id);
        if (m_selected && m_selected.getID() == id) {
            m_selected = {};
            if (!m_selectedIds.empty() && m_scene)
                m_selected = m_scene->getEntityByID(*m_selectedIds.begin());
        }
    } else {
        m_selectedIds.insert(id);
        m_selected = e;
    }
}

bool SceneHierarchyPanel::isSelected(EntityID id) const
{
    return m_selectedIds.contains(id) || (m_selected && m_selected.getID() == id);
}

bool SceneHierarchyPanel::entityMatchesFilter(Entity entity, const char* filter) const
{
    if (!entity)
        return false;
    if (!filter || filter[0] == '\0')
        return true;

    const auto* tag = m_scene->getComponent<TagComponent>(entity.getID());
    const std::string name = tag ? tag->tag : std::format("Entity {}", entity.getID());
    if (name.find(filter) != std::string::npos)
        return true;

    for (EntityID childId : m_scene->getChildren(entity.getID())) {
        if (entityMatchesFilter(m_scene->getEntityByID(childId), filter))
            return true;
    }

    return false;
}

Entity SceneHierarchyPanel::createEntityFromCommand(UINT command, EntityID parent)
{
    if (!m_scene)
        return {};

    Entity entity;
    switch (command) {
        case IdContextCreateEmpty:
            entity = m_scene->createEntity("Entity");
            break;
        case IdContextCreateMesh:
            entity = m_scene->createEntity("Mesh");
            entity.addComponent<TransformComponent>();
            entity.addComponent<MeshRendererComponent>();
            break;
        case IdContextCreateCamera:
            entity = m_scene->createEntity("Camera");
            entity.addComponent<TransformComponent>();
            entity.addComponent<CameraComponent>();
            break;
        case IdContextCreateReflectionProbe:
            entity = m_scene->createEntity("Reflection Probe");
            entity.addComponent<TransformComponent>();
            entity.addComponent<ReflectionProbeComponent>();
            break;
        case IdContextCreateIrradianceProbeVolume:
            entity = m_scene->createEntity("Irradiance Probe Volume");
            entity.addComponent<TransformComponent>();
            entity.addComponent<IrradianceProbeVolumeComponent>();
            break;
        case IdContextCreateVolumetricFog:
            entity = m_scene->createEntity("Volumetric Fog");
            entity.addComponent<TransformComponent>();
            entity.addComponent<VolumetricFogComponent>();
            break;
        case IdContextCreateLocalVolumetricFog:
            entity = m_scene->createEntity("Local Fog Volume");
            entity.addComponent<TransformComponent>();
            entity.addComponent<LocalVolumetricFogComponent>();
            break;
        case IdContextCreateVolumetricClouds:
            entity = m_scene->createEntity("Volumetric Clouds");
            entity.addComponent<TransformComponent>();
            entity.addComponent<VolumetricCloudComponent>();
            break;
        case IdContextCreateLensFlare:
            entity = m_scene->createEntity("Lens Flare");
            entity.addComponent<TransformComponent>();
            entity.addComponent<LensFlareComponent>();
            break;
        case IdContextCreateUIText:
        case IdContextCreateUIImage:
        case IdContextCreateUIShape:
        case IdContextCreateUISprite:
        case IdContextCreateUI3DText:
        case IdContextCreateUI3DImage: {
            const bool is3D = command == IdContextCreateUI3DText || command == IdContextCreateUI3DImage;
            const bool imageLike = command == IdContextCreateUIImage ||
                                   command == IdContextCreateUISprite ||
                                   command == IdContextCreateUI3DImage ||
                                   command == IdContextCreateUIShape;
            const char* name = "UI Text";
            UIElementKind kind = UIElementKind::Text2D;
            if (command == IdContextCreateUIImage) { name = "UI Image"; kind = UIElementKind::Image2D; }
            else if (command == IdContextCreateUIShape) { name = "UI Shape"; kind = UIElementKind::Shape2D; }
            else if (command == IdContextCreateUISprite) { name = "Sprite"; kind = UIElementKind::Sprite2D; }
            else if (command == IdContextCreateUI3DText) { name = "Text 3D"; kind = UIElementKind::Text3D; }
            else if (command == IdContextCreateUI3DImage) { name = "Image 3D"; kind = UIElementKind::Image3D; }

            entity = m_scene->createEntity(name);
            auto& transform = entity.addComponent<TransformComponent>();
            transform.translation = is3D ? glm::vec3{0.0f, 1.5f, 0.0f} : glm::vec3{0.0f, 0.0f, 0.0f};
            transform.scale = is3D ? glm::vec3{1.0f, 0.6f, 1.0f} : glm::vec3{1.0f, 1.0f, 1.0f};

            auto& ui = entity.addComponent<UIElementComponent>();
            ui.kind = kind;
            ui.screenSpace = !is3D;
            ui.billboard = is3D;
            ui.text = is3D ? "Text 3D" : "Text";
            ui.size = is3D ? glm::vec2{2.0f, 1.0f} : glm::vec2{240.0f, 80.0f};
            ui.color = command == IdContextCreateUIShape
                ? glm::vec4{0.2f, 0.65f, 1.0f, 1.0f}
                : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};

            if (imageLike) {
                auto& mesh = entity.addComponent<MeshRendererComponent>();
                mesh.meshPath = "builtin:plane";
                mesh.castShadows = false;
                mesh.receiveShadows = false;
                auto& material = entity.addComponent<MaterialComponent>();
                material.albedoColor = ui.color;
                material.doubleSided = true;
                material.alphaBlend = true;
            }
            break;
        }
        case IdContextCreateLightDirectional:
        case IdContextCreateLightPoint:
        case IdContextCreateLightSpot: {
            LightType type = LightType::Directional;
            const char* name = "Directional Light";
            if (command == IdContextCreateLightPoint) {
                type = LightType::Point;
                name = "Point Light";
            } else if (command == IdContextCreateLightSpot) {
                type = LightType::Spot;
                name = "Spot Light";
            }

            entity = m_scene->createEntity(name);
            entity.addComponent<TransformComponent>();
            auto& light = entity.addComponent<LightComponent>();
            light.type = type;
            break;
        }
        case IdContextCreateLandscapeTerrain: {
            entity = m_scene->createEntity("Terrain");
            entity.addComponent<TransformComponent>();
            entity.addComponent<TerrainComponent>();
            entity.addComponent<TerrainSculptComponent>();
            entity.addComponent<TerrainFoliageComponent>();
            break;
        }
        case IdContextCreateFluidLake:
        case IdContextCreateFluidRiver:
        case IdContextCreateFluidOcean:
        case IdContextCreateFluidPool:
        case IdContextCreateFluidCustomArea: {
            entity = m_scene->createEntity("Water");
            auto& transform = entity.addComponent<TransformComponent>();
            auto& water = entity.addComponent<WaterBodyComponent>();

            if (command == IdContextCreateFluidLake) {
                if (auto* tag = m_scene->getComponent<TagComponent>(entity.getID()))
                    tag->tag = "Lake";
                water.type = WaterBodyType::Lake;
                water.size = {36.0f, 28.0f};
                water.depth = 4.5f;
                water.waveAmplitude = 0.18f;
                water.waveLength = 9.0f;
                water.waveSpeed = 0.85f;
                water.choppiness = 0.35f;
                water.roughness = 0.08f;
                water.foamIntensity = 0.10f;
                water.edgeFade = 0.18f;
                water.surfaceColor = {0.08f, 0.36f, 0.32f, 0.82f};
                water.bottomColor = {0.02f, 0.10f, 0.16f, 0.40f};
            } else if (command == IdContextCreateFluidRiver) {
                if (auto* tag = m_scene->getComponent<TagComponent>(entity.getID()))
                    tag->tag = "River";
                water.type = WaterBodyType::River;
                water.size = {52.0f, 12.0f};
                water.depth = 2.8f;
                water.waveAmplitude = 0.22f;
                water.waveLength = 6.5f;
                water.waveSpeed = 1.65f;
                water.choppiness = 0.55f;
                water.roughness = 0.07f;
                water.foamIntensity = 0.24f;
                water.edgeFade = 0.08f;
                water.surfaceColor = {0.08f, 0.31f, 0.28f, 0.80f};
                water.bottomColor = {0.01f, 0.07f, 0.12f, 0.38f};
                water.flowDirection = {1.0f, 0.15f};
                water.flowSpeed = 2.6f;
            } else if (command == IdContextCreateFluidOcean) {
                if (auto* tag = m_scene->getComponent<TagComponent>(entity.getID()))
                    tag->tag = "Ocean";
                water.type = WaterBodyType::Ocean;
                water.size = {160.0f, 160.0f};
                water.depth = 14.0f;
                water.waveAmplitude = 0.65f;
                water.waveLength = 18.0f;
                water.waveSpeed = 0.95f;
                water.choppiness = 1.30f;
                water.roughness = 0.04f;
                water.foamIntensity = 0.82f;
                water.edgeFade = 0.04f;
                water.surfaceColor = {0.07f, 0.29f, 0.31f, 0.86f};
                water.bottomColor = {0.01f, 0.05f, 0.14f, 0.44f};
            } else if (command == IdContextCreateFluidPool) {
                if (auto* tag = m_scene->getComponent<TagComponent>(entity.getID()))
                    tag->tag = "Pool";
                water.type = WaterBodyType::Pool;
                water.size = {18.0f, 10.0f};
                water.depth = 2.2f;
                water.waveAmplitude = 0.08f;
                water.waveLength = 5.5f;
                water.waveSpeed = 0.55f;
                water.choppiness = 0.12f;
                water.roughness = 0.03f;
                water.foamIntensity = 0.02f;
                water.edgeFade = 0.24f;
                water.surfaceColor = {0.18f, 0.54f, 0.62f, 0.74f};
                water.bottomColor = {0.05f, 0.16f, 0.23f, 0.36f};
            } else {
                if (auto* tag = m_scene->getComponent<TagComponent>(entity.getID()))
                    tag->tag = "Custom Water";
                water.type = WaterBodyType::CustomArea;
                water.size = {24.0f, 24.0f};
                water.depth = 3.0f;
                water.waveAmplitude = 0.24f;
                water.waveLength = 8.0f;
                water.waveSpeed = 1.0f;
                water.choppiness = 0.65f;
                water.roughness = 0.06f;
                water.foamIntensity = 0.20f;
                water.edgeFade = 0.16f;
            }

            transform.translation.y = 0.0f;
            break;
        }
        default:
            return {};
    }

    if (entity && parent != NULL_ENTITY)
        m_scene->setParent(entity.getID(), parent);
    if (entity && parent != NULL_ENTITY) {
        if (auto* transform = m_scene->getComponent<TransformComponent>(entity.getID()))
            *transform = TransformComponent{};
    }

    if (entity) {
        setSelectedEntity(entity);
        m_sceneEdited = true;
    }

    return entity;
}

void SceneHierarchyPanel::render()
{
    ImGui::Begin("Hierarchy", nullptr, editorPanelFlags());
    m_entityClickedThisFrame = false;

    static char filter[128] = "";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search...", filter, sizeof(filter));
    ImGui::Separator();

    if (m_scene) {
        for (EntityID id : m_scene->getEntities()) {
            if (m_scene->getParent(id) != NULL_ENTITY)
                continue;
            drawEntityNode(m_scene->getEntityByID(id), filter);
        }
    }

    if (ImGui::IsWindowHovered() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        !ImGui::IsAnyItemHovered())
    {
        HMENU contextMenu = createEntityMenu("Create Entity");
        const UINT command = showPopupMenu(contextMenu);
        createEntityFromCommand(command);
        DestroyMenu(contextMenu);
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
            const EntityID dropped = *static_cast<const EntityID*>(payload->Data);
            if (dropped != NULL_ENTITY) {
                m_scene->clearParent(dropped);
                m_sceneEdited = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsWindowHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() &&
        !m_entityClickedThisFrame)
        setSelectedEntity({});

    ImGui::End();
}

void SceneHierarchyPanel::drawEntityNode(Entity entity, const char* filter)
{
    if (!entity || !entityMatchesFilter(entity, filter))
        return;

    auto* tag = entity.hasComponent<TagComponent>() ? &entity.getComponent<TagComponent>() : nullptr;
    const std::string name = tag ? tag->tag : std::format("Entity {}", entity.getID());
    const auto& children = m_scene->getChildren(entity.getID());
    const bool hasChildren = !children.empty();

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (isSelected(entity.getID()))
        flags |= ImGuiTreeNodeFlags_Selected | ImGuiTreeNodeFlags_DefaultOpen;

    const char* icon = "  ";
    if (entity.hasComponent<LightComponent>()) icon = "[Light] ";
    else if (entity.hasComponent<CameraComponent>()) icon = "[Cam] ";
    else if (entity.hasComponent<VolumetricCloudComponent>()) icon = "[Cloud] ";
    else if (entity.hasComponent<VolumetricFogComponent>()) icon = "[VFog] ";
    else if (entity.hasComponent<LocalVolumetricFogComponent>()) icon = "[LFog] ";
    else if (entity.hasComponent<LensFlareComponent>()) icon = "[Flare] ";
    else if (entity.hasComponent<ReflectionProbeComponent>()) icon = "[Probe] ";
    else if (entity.hasComponent<IrradianceProbeVolumeComponent>()) icon = "[DDGI] ";
    else if (entity.hasComponent<UIElementComponent>()) icon = "[UI] ";
    else if (entity.hasComponent<MeshRendererComponent>()) icon = "[Mesh] ";
    else if (entity.hasComponent<TerrainComponent>()) icon = "[Terrain] ";
    else if (entity.hasComponent<WaterBodyComponent>()) icon = "[Water] ";

    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(entity.getID())),
                                        flags,
                                        "%s%s",
                                        icon,
                                        name.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if (ImGui::GetIO().KeyCtrl)
            toggleSelectedEntity(entity);
        else
            setSelectedEntity(entity);
        m_entityClickedThisFrame = true;
    }

    if (ImGui::BeginDragDropSource()) {
        const EntityID id = entity.getID();
        ImGui::SetDragDropPayload("SCENE_HIERARCHY_ENTITY", &id, sizeof(id));
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_HIERARCHY_ENTITY")) {
            const EntityID dropped = *static_cast<const EntityID*>(payload->Data);
            if (dropped != NULL_ENTITY && dropped != entity.getID()) {
                if (m_scene->setParent(dropped, entity.getID()))
                    m_sceneEdited = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        setSelectedEntity(entity);

        HMENU createChildMenu = createEntityMenu("Create Child");
        HMENU contextMenu = CreatePopupMenu();
        AppendMenuA(contextMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(createChildMenu), "Create Child");
        AppendMenuA(contextMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(contextMenu, MF_STRING, IdContextDuplicate, "Duplicate");
        AppendMenuA(contextMenu, MF_STRING, IdContextRename, "Rename");
        AppendMenuA(contextMenu,
                    (m_scene->getParent(entity.getID()) != NULL_ENTITY) ? MF_STRING : MF_STRING | MF_GRAYED,
                    IdContextUnparent,
                    "Unparent");
        AppendMenuA(contextMenu,
                    hasChildren ? MF_STRING : MF_STRING | MF_GRAYED,
                    IdContextDeleteChildren,
                    "Delete Children");
        AppendMenuA(contextMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(contextMenu, MF_STRING, IdContextDelete, "Delete Entity");

        const UINT command = showPopupMenu(contextMenu);
        switch (command) {
            case IdContextDuplicate:
                if (Entity duplicate = m_scene->duplicateEntity(entity.getID())) {
                    setSelectedEntity(duplicate);
                    m_sceneEdited = true;
                }
                break;
            case IdContextRename:
                m_renaming = entity.getID();
                break;
            case IdContextUnparent:
                m_scene->clearParent(entity.getID());
                m_sceneEdited = true;
                break;
            case IdContextDeleteChildren: {
                const auto childrenCopy = children;
                for (EntityID childId : childrenCopy)
                    m_scene->destroyEntity(childId);
                m_sceneEdited = true;
                break;
            }
            case IdContextDelete:
                m_scene->destroyEntity(entity.getID());
                if (m_selected && m_selected.getID() == entity.getID())
                    setSelectedEntity({});
                m_sceneEdited = true;
                break;
            default:
                createEntityFromCommand(command, entity.getID());
                break;
        }

        DestroyMenu(contextMenu);
        if (!m_scene->entityExists(entity.getID())) {
            if (open)
                ImGui::TreePop();
            return;
        }
    }

    if (m_renaming == entity.getID()) {
        static char renameBuffer[256];
        static EntityID renameBufferEntity = NULL_ENTITY;
        if (tag) {
            if (renameBufferEntity != entity.getID()) {
                copyStringToBuffer(renameBuffer, tag->tag);
                renameBufferEntity = entity.getID();
            }
        }
        ImGui::SameLine();
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename",
                             renameBuffer,
                             sizeof(renameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll))
        {
            if (tag)
                tag->tag = renameBuffer;
            m_renaming = NULL_ENTITY;
            renameBufferEntity = NULL_ENTITY;
            m_sceneEdited = true;
        }
        if (!ImGui::IsItemActive()) {
            m_renaming = NULL_ENTITY;
            renameBufferEntity = NULL_ENTITY;
        }
    }

    if (open) {
        for (EntityID childId : children)
            drawEntityNode(m_scene->getEntityByID(childId), filter);
        ImGui::TreePop();
    }
}

} // namespace Demon
