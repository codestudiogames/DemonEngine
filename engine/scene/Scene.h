#pragma once
// ==============================================================================
//  DemonEngine::Scene
//  Lightweight scene graph.  Entities are uint64 handles; components are stored
//  in type-erased maps.  Simple, fast, and friendly to indie devs.
//
//  Optimisations vs original:
//   - Mesh cache, missing-mesh set, material caches are Scene members instead
//     of local statics, so they are properly destroyed with the scene and don't
//     leak across scene reloads.
//   - MaterialComponent now carries a bool dirty flag so Scene::onRender can
//     skip redundant material syncs on frames where nothing changed.
// ==============================================================================
#include "core/DemonPCH.h"
#include "core/UUID.h"
#include "core/Logger.h"
#include "Components.h"
#include "physics/CollisionSystem.h"
#include "physics/PhysicsWorld.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include <any>
#include <typeindex>

namespace Demon {

using EntityID = uint64_t;
constexpr EntityID NULL_ENTITY = 0;

// ── Component storage per-type ────────────────────────────────────────────────
class ComponentPool {
public:
    template<typename T>
    void add(EntityID id, T&& component) {
        auto key = std::type_index(typeid(T));
        m_data[key][id] = std::forward<T>(component);
    }

    template<typename T>
    T* get(EntityID id) {
        auto key = std::type_index(typeid(T));
        auto it  = m_data.find(key);
        if (it == m_data.end()) return nullptr;
        auto jt = it->second.find(id);
        if (jt == it->second.end()) return nullptr;
        return std::any_cast<T>(&jt->second);
    }

    template<typename T>
    const T* get(EntityID id) const {
        auto key = std::type_index(typeid(T));
        auto it  = m_data.find(key);
        if (it == m_data.end()) return nullptr;
        auto jt = it->second.find(id);
        if (jt == it->second.end()) return nullptr;
        return std::any_cast<T>(&jt->second);
    }

    template<typename T>
    bool has(EntityID id) { return get<T>(id) != nullptr; }

    template<typename T>
    bool has(EntityID id) const { return get<T>(id) != nullptr; }

    template<typename T>
    void remove(EntityID id) {
        auto key = std::type_index(typeid(T));
        if (auto it = m_data.find(key); it != m_data.end())
            it->second.erase(id);
    }

    void removeAll(EntityID id) {
        for (auto& [key, map] : m_data)
            map.erase(id);
    }

    template<typename T>
    std::vector<std::pair<EntityID, T*>> view() {
        std::vector<std::pair<EntityID, T*>> result;
        auto key = std::type_index(typeid(T));
        if (auto it = m_data.find(key); it != m_data.end())
            for (auto& [id, val] : it->second)
                result.emplace_back(id, std::any_cast<T>(&val));
        return result;
    }

    template<typename T>
    std::vector<std::pair<EntityID, const T*>> view() const {
        std::vector<std::pair<EntityID, const T*>> result;
        auto key = std::type_index(typeid(T));
        if (auto it = m_data.find(key); it != m_data.end())
            for (const auto& [id, val] : it->second)
                result.emplace_back(id, std::any_cast<T>(&val));
        return result;
    }

    void clearTypeData(std::type_index key) {
        m_data.erase(key);
    }

private:
    std::unordered_map<std::type_index,
        std::unordered_map<EntityID, std::any>> m_data;
};

// ─────────────────────────────────────────────────────────────────────────────
class Entity;

class Scene {
public:
    explicit Scene(std::string name = "Untitled Scene");
    Scene(const Scene& other);
    Scene& operator=(const Scene& other);
    ~Scene() = default;

    // ── Entity management ─────────────────────────────────────────────────────
    Entity createEntity(std::string tag = "Entity");
    Entity createEntityWithID(UUID id, std::string tag = "Entity");
    Entity duplicateEntity(EntityID source, EntityID parentOverride = NULL_ENTITY);
    void   destroyEntity(EntityID id);
    bool   entityExists(EntityID id) const;

    Entity getEntityByTag(std::string_view tag);
    Entity getEntityByID(EntityID id);

    [[nodiscard]] const std::vector<EntityID>& getEntities() const { return m_entities; }
    template<typename Fn>
    void forEachEntity(Fn&& fn);

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void onUpdate(float dt);
    void onRender(class Renderer& renderer);
    void onViewportResize(uint32_t width, uint32_t height);
    void setSimulationEnabled(bool enabled) { m_simulationEnabled = enabled; }
    [[nodiscard]] bool isSimulationEnabled() const { return m_simulationEnabled; }
    void setSelectedEntity(EntityID id) { m_selected = id; }
    EntityID getSelectedEntity() const  { return m_selected; }
    bool setParent(EntityID child, EntityID parent);
    void clearParent(EntityID child);
    [[nodiscard]] EntityID getParent(EntityID id) const;
    [[nodiscard]] const std::vector<EntityID>& getChildren(EntityID id) const;
    [[nodiscard]] bool isDescendantOf(EntityID entity, EntityID potentialAncestor) const;
    [[nodiscard]] glm::mat4 getWorldTransform(EntityID id) const;
    [[nodiscard]] EntityID getPrimaryCameraID() const;
    [[nodiscard]] EntityID pickEntity(const glm::vec3& rayOrigin, const glm::vec3& rayDir);
    std::shared_ptr<Mesh> getResolvedMesh(EntityID id);
    [[nodiscard]] float getElapsedTime() const { return m_elapsedTime; }
    [[nodiscard]] float sampleGroundAtWorld(float worldX,
                                            float worldZ,
                                            float probeTopY,
                                            float maxSnapDown,
                                            glm::vec3* outNormal = nullptr);

    // ── Component access ──────────────────────────────────────────────────────
    template<typename T> T*   addComponent(EntityID id, T comp)    { m_pool.add(id, std::move(comp)); return m_pool.get<T>(id); }
    template<typename T> T*   getComponent(EntityID id)            { return m_pool.get<T>(id); }
    template<typename T> const T* getComponent(EntityID id) const  { return m_pool.get<T>(id); }
    template<typename T> bool hasComponent(EntityID id)            { return m_pool.has<T>(id); }
    template<typename T> bool hasComponent(EntityID id) const      { return m_pool.has<T>(id); }
    template<typename T> void removeComponent(EntityID id)         { m_pool.remove<T>(id); }
    template<typename T> auto view()                               { return m_pool.view<T>(); }
    template<typename T> auto view() const                         { return m_pool.view<T>(); }

    [[nodiscard]] const std::string& getName() const { return m_name; }
    void setName(std::string n) { m_name = std::move(n); }

    // Collision data (main-thread only for updates)
    CollisionSystem& getCollisionSystem() { return *m_collisionSystem; }
    const CollisionSystem& getCollisionSystem() const { return *m_collisionSystem; }

    // ── Static factory ────────────────────────────────────────────────────────
    static std::shared_ptr<Scene> create(std::string name = "Untitled Scene");
    static std::shared_ptr<Scene> copy(const std::shared_ptr<Scene>& src);

private:
    struct ProceduralRenderCache {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        float lastAnimatedTime = -1.0f;
    };

    std::shared_ptr<Mesh> resolveMeshAsset(const MeshRendererComponent& mr);
    void updatePhysics(float dt);

    std::string           m_name;
    std::vector<EntityID> m_entities;
    ComponentPool         m_pool;
    uint64_t              m_nextID  = 1;
    EntityID              m_selected = NULL_ENTITY;
    bool                  m_simulationEnabled = false;
    float                 m_elapsedTime = 0.0f;

    // ── Per-scene render caches (previously local statics in onRender) ────────
    // Storing these as members means they are properly destroyed when the scene
    // is destroyed, and are never accidentally shared between scene instances.
    std::unordered_map<std::string, std::shared_ptr<Mesh>>     m_meshCache;
    std::unordered_set<std::string>                             m_missingMeshes;
    std::unordered_map<std::string, std::shared_ptr<Material>>  m_materialPathCache;
    std::unordered_map<EntityID,    std::shared_ptr<Material>>  m_materialEntityCache;
    std::unordered_map<EntityID,    bool>                       m_materialDirtyFlags;
    std::unordered_map<EntityID,    ProceduralRenderCache>      m_terrainRenderCache;
    std::unordered_map<EntityID,    ProceduralRenderCache>      m_foliageRenderCache;
    std::unordered_map<EntityID,    ProceduralRenderCache>      m_waterRenderCache;

    std::unique_ptr<CollisionSystem>                            m_collisionSystem;
    std::unique_ptr<PhysicsWorld>                               m_physicsWorld;

    friend class Entity;
    friend class SceneSerializer;
};

// ── Entity handle ─────────────────────────────────────────────────────────────
class Entity {
public:
    Entity() = default;
    Entity(EntityID id, Scene* scene) : m_id(id), m_scene(scene) {}

    template<typename T, typename... Args>
    T& addComponent(Args&&... args) {
        auto* ptr = m_scene->addComponent(m_id, T{std::forward<Args>(args)...});
        DEMON_ASSERT(ptr, "addComponent failed");
        return *ptr;
    }

    template<typename T> T&   getComponent()        { return *m_scene->getComponent<T>(m_id); }
    template<typename T> bool hasComponent()  const { return m_scene->hasComponent<T>(m_id); }
    template<typename T> void removeComponent()     { m_scene->removeComponent<T>(m_id); }

    [[nodiscard]] EntityID getID() const { return m_id; }
    operator bool() const { return m_id != NULL_ENTITY && m_scene != nullptr; }
    bool operator==(const Entity& o) const { return m_id == o.m_id && m_scene == o.m_scene; }

private:
    EntityID m_id    = NULL_ENTITY;
    Scene*   m_scene = nullptr;
};

template<typename Fn>
void Scene::forEachEntity(Fn&& fn) {
    for (auto id : m_entities)
        fn(Entity(id, this));
}

} // namespace Demon
