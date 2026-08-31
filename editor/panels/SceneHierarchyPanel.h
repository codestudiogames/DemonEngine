#pragma once
// ==============================================================================
//  DemonEngine Editor::SceneHierarchyPanel
//  Displays all scene entities in a tree.
//  Right-click to create/delete; drag to reparent.
// ==============================================================================
#include "core/DemonPCH.h"
#include "scene/Scene.h"
#include "scene/Components.h"

namespace Demon {

class SceneHierarchyPanel {
public:
    explicit SceneHierarchyPanel(std::shared_ptr<Scene> scene);

    void setScene(std::shared_ptr<Scene> scene);
    void render();

    [[nodiscard]] Entity getSelectedEntity() const { return m_selected; }
    [[nodiscard]] std::vector<EntityID> getSelectedEntityIDs() const;
    void setSelectedEntity(Entity e);
    void selectAllEntities();
    void toggleSelectedEntity(Entity e);
    bool isSelected(EntityID id) const;
    bool consumeSceneEdited() { bool v = m_sceneEdited; m_sceneEdited = false; return v; }

private:
    void drawEntityNode(Entity entity, const char* filter);
    Entity createEntityFromCommand(UINT command, EntityID parent = NULL_ENTITY);
    bool entityMatchesFilter(Entity entity, const char* filter) const;

    std::shared_ptr<Scene> m_scene;
    Entity                 m_selected;
    std::unordered_set<EntityID> m_selectedIds;
    EntityID               m_renaming = NULL_ENTITY;
    bool                   m_entityClickedThisFrame = false;
    bool                   m_sceneEdited = false;
};

} // namespace Demon
