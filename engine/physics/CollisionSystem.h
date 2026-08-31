#pragma once
// ==============================================================================
//  DemonEngine::CollisionSystem
//  Broadphase + Narrowphase collision detection for Scene colliders.
// ==============================================================================
#include "core/DemonPCH.h"
#include "physics/Narrowphase.h"

namespace Demon {

class Scene;

struct CollisionPair {
    uint64_t a = 0;
    uint64_t b = 0;
    ContactManifold manifold{};
    bool isTrigger = false;
};

class CollisionSystem {
public:
    CollisionSystem() = default;

    // Main-thread only: reads Scene component storage.
    void update(Scene& scene);
    void clear();
    void setPairs(std::vector<CollisionPair> pairs);

    [[nodiscard]] const std::vector<CollisionPair>& getPairs() const { return m_pairs; }

private:
    struct AABB {
        glm::vec3 min = {0, 0, 0};
        glm::vec3 max = {0, 0, 0};
        [[nodiscard]] bool overlaps(const AABB& o) const {
            return min.x <= o.max.x && max.x >= o.min.x &&
                   min.y <= o.max.y && max.y >= o.min.y &&
                   min.z <= o.max.z && max.z >= o.min.z;
        }
    };

    struct Proxy {
        uint64_t id = 0;
        BoxShape shape;
        AABB aabb;
        bool isTrigger = false;
    };

    std::vector<Proxy> m_proxies;
    std::vector<CollisionPair> m_pairs;
};

} // namespace Demon
