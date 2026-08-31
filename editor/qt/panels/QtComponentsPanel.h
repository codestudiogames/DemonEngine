#pragma once

#include "scene/Scene.h"

#include <QScrollArea>
#include <functional>
#include <memory>

namespace Demon {

class QtComponentsPanel final : public QScrollArea {
public:
    explicit QtComponentsPanel(QWidget* parent = nullptr);

    void setScene(std::shared_ptr<Scene> scene);
    void setSelectedEntity(EntityID entity);
    void setChangedCallback(std::function<void()> callback) { m_changed = std::move(callback); }
    void setHierarchyChangedCallback(std::function<void()> callback) { m_hierarchyChanged = std::move(callback); }
    void refresh();

private:
    void markChanged();

    std::shared_ptr<Scene> m_scene;
    EntityID m_entity = NULL_ENTITY;
    std::function<void()> m_changed;
    std::function<void()> m_hierarchyChanged;
};

} // namespace Demon
