#pragma once

#include "core/DemonPCH.h"
#include "renderer/Renderer.h"
#include "scene/Scene.h"

namespace Demon {

class EnvironmentPanel {
public:
    void render(Renderer& renderer, const std::shared_ptr<Scene>& scene);
    bool consumeSceneEdited() { const bool edited = m_sceneEdited; m_sceneEdited = false; return edited; }
    bool consumeGiApplyRequested() { const bool requested = m_giApplyRequested; m_giApplyRequested = false; return requested; }
    bool consumeShaderReloadRequested() { const bool requested = m_shaderReloadRequested; m_shaderReloadRequested = false; return requested; }

private:
    template<typename T>
    T* firstComponent(const std::shared_ptr<Scene>& scene, EntityID* outId = nullptr) const
    {
        if (!scene)
            return nullptr;
        for (auto [id, component] : scene->view<T>()) {
            if (component) {
                if (outId)
                    *outId = id;
                return component;
            }
        }
        return nullptr;
    }

    template<typename T>
    T& ensureWorldComponent(const std::shared_ptr<Scene>& scene, const char* entityName)
    {
        EntityID existing = NULL_ENTITY;
        if (T* component = firstComponent<T>(scene, &existing))
            return *component;

        Entity environment = scene->createEntity(entityName);
        if (!environment.hasComponent<TransformComponent>())
            environment.addComponent<TransformComponent>();
        m_sceneEdited = true;
        return environment.addComponent<T>();
    }

    void applyWorldPreset(RendererSettings& settings, int preset);

    bool m_sceneEdited = false;
    bool m_giApplyRequested = false;
    bool m_shaderReloadRequested = false;
};

} // namespace Demon
