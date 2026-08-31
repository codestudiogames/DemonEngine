#pragma once

#include "core/DemonPCH.h"

namespace Demon {

class CollisionSystem;
class Scene;

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    [[nodiscard]] bool isJoltAvailable() const;
    void clear();

    // Returns true when an external backend stepped the scene. Callers should
    // fall back to DemonPhysics when this returns false.
    bool step(Scene& scene, float dt, CollisionSystem& collisions);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Demon
