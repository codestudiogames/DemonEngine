// ==============================================================================
//  DemonEngine::CollisionSystem  -  Implementation
// ==============================================================================
#include "CollisionSystem.h"
#include "scene/Scene.h"
#include "core/Logger.h"

namespace Demon {

void CollisionSystem::clear()
{
    m_proxies.clear();
    m_pairs.clear();
}

void CollisionSystem::setPairs(std::vector<CollisionPair> pairs)
{
    m_proxies.clear();
    m_pairs = std::move(pairs);
}

void CollisionSystem::update(Scene& scene)
{
    m_proxies.clear();
    m_pairs.clear();

    auto colliders = scene.view<BoxColliderComponent>();
    m_proxies.reserve(colliders.size());

    for (auto [id, bc] : colliders) {
        if (!bc) continue;
        auto* tr = scene.getComponent<TransformComponent>(id);
        if (!tr) continue;

        glm::vec3 scale = tr->scale;
        glm::vec3 half = glm::abs(scale) * bc->halfExtents;
        glm::mat3 rot = glm::mat3(glm::eulerAngleXYZ(
            glm::radians(tr->rotation.x),
            glm::radians(tr->rotation.y),
            glm::radians(tr->rotation.z)));

        glm::vec3 center = tr->translation + rot * bc->offset;

        BoxShape shape;
        shape.center = center;
        shape.orientation = rot;
        shape.halfExtents = half;

        Proxy p;
        p.id = static_cast<uint64_t>(id);
        p.shape = shape;
        glm::mat3 absRot = glm::mat3(
            glm::abs(shape.orientation[0]),
            glm::abs(shape.orientation[1]),
            glm::abs(shape.orientation[2]));
        glm::vec3 ext = absRot * shape.halfExtents;
        p.aabb.min = shape.center - ext;
        p.aabb.max = shape.center + ext;
        p.isTrigger = bc->isTrigger;
        m_proxies.push_back(p);
    }

    const size_t n = m_proxies.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const Proxy& a = m_proxies[i];
            const Proxy& b = m_proxies[j];
            if (!a.aabb.overlaps(b.aabb)) continue;

            ContactManifold manifold;
            if (Narrowphase::computeManifold(a.shape, b.shape, manifold)) {
                CollisionPair pair;
                pair.a = a.id;
                pair.b = b.id;
                pair.manifold = manifold;
                pair.isTrigger = a.isTrigger || b.isTrigger;
                m_pairs.push_back(pair);
            }
        }
    }
}

} // namespace Demon
