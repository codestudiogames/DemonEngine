#pragma once
// ==============================================================================
//  DemonEngine::Narrowphase
//  GJK + EPA collision detection for convex shapes with contact manifolds.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

struct ContactPoint {
    glm::vec3 position = {0, 0, 0};
    float     penetration = 0.0f;
};

struct ContactManifold {
    glm::vec3 normal = {0, 1, 0};
    float     penetration = 0.0f;
    std::array<ContactPoint, 4> points{};
    uint32_t  pointCount = 0;
};

struct SupportPoint {
    glm::vec3 point  = {0, 0, 0}; // Minkowski point
    glm::vec3 pointA = {0, 0, 0}; // Support point on A
    glm::vec3 pointB = {0, 0, 0}; // Support point on B
};

struct BoxShape {
    glm::vec3 center = {0, 0, 0};
    glm::mat3 orientation = glm::mat3(1.0f);
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};

    [[nodiscard]] glm::vec3 support(const glm::vec3& dir) const;
};

class Narrowphase {
public:
    // Thread-safe: stateless narrowphase evaluation.
    static bool computeManifold(const BoxShape& a, const BoxShape& b, ContactManifold& out);
};

} // namespace Demon
