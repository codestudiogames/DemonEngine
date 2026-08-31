#include "PhysicsWorld.h"

#include "CollisionSystem.h"
#include "core/Logger.h"
#include "scene/Scene.h"

#ifdef DEMON_USE_JOLT
#   include <Jolt/Jolt.h>
#   include <Jolt/RegisterTypes.h>
#   include <Jolt/Core/Factory.h>
#   include <Jolt/Core/JobSystemThreadPool.h>
#   include <Jolt/Core/TempAllocator.h>
#   include <Jolt/Physics/Body/Body.h>
#   include <Jolt/Physics/Body/BodyCreationSettings.h>
#   include <Jolt/Physics/Body/BodyInterface.h>
#   include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#   include <Jolt/Physics/Collision/ContactListener.h>
#   include <Jolt/Physics/Collision/Shape/BoxShape.h>
#   include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#   include <Jolt/Physics/PhysicsSystem.h>
#   include <thread>
JPH_SUPPRESS_WARNINGS
#endif

namespace Demon {

#ifdef DEMON_USE_JOLT
namespace {

JPH::Vec3 toJolt(const glm::vec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

JPH::RVec3 toJoltR(const glm::vec3& v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

glm::vec3 fromJoltVec3(const JPH::Vec3& v)
{
    return {v.GetX(), v.GetY(), v.GetZ()};
}

glm::vec3 fromJoltRVec3(const JPH::RVec3& v)
{
    return {static_cast<float>(v.GetX()),
            static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())};
}

JPH::Quat toJolt(const glm::quat& q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

glm::quat fromJolt(const JPH::Quat& q)
{
    return glm::normalize(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()));
}

glm::quat rotationQuat(const TransformComponent& transform)
{
    return glm::normalize(glm::quat(glm::radians(transform.rotation)));
}

JPH::EMotionType toMotionType(const RigidBodyComponent& body)
{
    if (body.type == BodyType::Static)
        return JPH::EMotionType::Static;
    if (body.type == BodyType::Kinematic || body.isKinematic)
        return JPH::EMotionType::Kinematic;
    return JPH::EMotionType::Dynamic;
}

JPH::EAllowedDOFs allowedDofs(const RigidBodyComponent& body)
{
    JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::None;
    if (!body.lockPositionX) dofs |= JPH::EAllowedDOFs::TranslationX;
    if (!body.lockPositionY) dofs |= JPH::EAllowedDOFs::TranslationY;
    if (!body.lockPositionZ) dofs |= JPH::EAllowedDOFs::TranslationZ;

    const bool lockAllRotations = body.lockRotation;
    if (!lockAllRotations && !body.lockRotationX) dofs |= JPH::EAllowedDOFs::RotationX;
    if (!lockAllRotations && !body.lockRotationY) dofs |= JPH::EAllowedDOFs::RotationY;
    if (!lockAllRotations && !body.lockRotationZ) dofs |= JPH::EAllowedDOFs::RotationZ;

    if (dofs == JPH::EAllowedDOFs::None)
        return JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
    return dofs;
}

namespace Layers {
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
}

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer Static(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr uint32_t Count = 2;
}

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        if (a == Layers::Static)
            return b == Layers::Moving;
        if (a == Layers::Moving)
            return true;
        return false;
    }
};

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterface()
    {
        m_objectToBroadPhase[Layers::Static] = BroadPhaseLayers::Static;
        m_objectToBroadPhase[Layers::Moving] = BroadPhaseLayers::Moving;
    }

    uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return m_objectToBroadPhase[layer < Layers::Count ? layer : Layers::Static];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return static_cast<JPH::BroadPhaseLayer::Type>(layer) == static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Static)
            ? "Static"
            : "Moving";
    }
#endif

private:
    JPH::BroadPhaseLayer m_objectToBroadPhase[Layers::Count];
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
    {
        if (layer == Layers::Static)
            return broadPhaseLayer == BroadPhaseLayers::Moving;
        if (layer == Layers::Moving)
            return true;
        return false;
    }
};

class ContactCollector final : public JPH::ContactListener {
public:
    void beginFrame()
    {
        std::scoped_lock lock(m_mutex);
        m_pairs.clear();
    }

    std::vector<CollisionPair> consume()
    {
        std::scoped_lock lock(m_mutex);
        return std::move(m_pairs);
    }

    void OnContactAdded(const JPH::Body& a,
                        const JPH::Body& b,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override
    {
        pushPair(a, b, manifold, settings.mIsSensor);
    }

    void OnContactPersisted(const JPH::Body& a,
                            const JPH::Body& b,
                            const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override
    {
        pushPair(a, b, manifold, settings.mIsSensor);
    }

private:
    void pushPair(const JPH::Body& a,
                  const JPH::Body& b,
                  const JPH::ContactManifold& manifold,
                  bool sensor)
    {
        CollisionPair pair;
        pair.a = a.GetUserData();
        pair.b = b.GetUserData();
        pair.manifold.normal = fromJoltVec3(manifold.mWorldSpaceNormal);
        pair.manifold.penetration = std::max(manifold.mPenetrationDepth, 0.0f);
        pair.isTrigger = sensor || a.IsSensor() || b.IsSensor();

        std::scoped_lock lock(m_mutex);
        m_pairs.push_back(pair);
    }

    std::mutex m_mutex;
    std::vector<CollisionPair> m_pairs;
};

void ensureJoltRegistered()
{
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        if (!JPH::Factory::sInstance)
            JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        DEMON_LOG_INFO("Physics: Jolt initialized.");
    });
}

struct JoltRegistrationGuard {
    JoltRegistrationGuard()
    {
        ensureJoltRegistered();
    }
};

} // namespace

struct PhysicsWorld::Impl {
    Impl()
        : tempAllocator(16 * 1024 * 1024),
          jobSystem(JPH::cMaxPhysicsJobs,
                    JPH::cMaxPhysicsBarriers,
                    static_cast<int>(std::max(1u, std::thread::hardware_concurrency() > 0
                                                     ? std::thread::hardware_concurrency() - 1
                                                     : 1u)))
    {
        resetSystem();
    }

    void clear()
    {
        entityToBody.clear();
        signature = 0;
        resetSystem();
    }

    bool step(Scene& scene, float dt, CollisionSystem& collisions)
    {
        if (dt <= 0.0f) {
            collisions.update(scene);
            return true;
        }

        const uint64_t sceneSignature = computeSignature(scene);
        if (sceneSignature != signature) {
            signature = sceneSignature;
            rebuild(scene);
        }

        if (entityToBody.empty()) {
            collisions.update(scene);
            return true;
        }

        syncSceneToBodies(scene);
        contacts.beginFrame();
        system->Update(std::min(dt, 1.0f / 15.0f), 1, &tempAllocator, &jobSystem);
        syncBodiesToScene(scene);

        std::vector<CollisionPair> pairs = contacts.consume();
        if (pairs.empty())
            collisions.update(scene);
        else
            collisions.setPairs(std::move(pairs));
        return true;
    }

    void resetSystem()
    {
        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(65536, 0, 65536, 20480, broadPhaseLayers, objectVsBroadPhase, objectLayerPairs);
        system->SetContactListener(&contacts);
    }

    uint64_t computeSignature(Scene& scene) const
    {
        uint64_t hash = 1469598103934665603ull;
        auto mix = [&](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };

        for (auto [id, body] : scene.view<RigidBodyComponent>()) {
            if (!body || !body->simulatePhysics)
                continue;
            const auto* collider = scene.getComponent<BoxColliderComponent>(id);
            const auto* transform = scene.getComponent<TransformComponent>(id);
            if (!collider || !transform)
                continue;

            mix(id);
            mix(static_cast<uint64_t>(body->type));
            mix(body->isKinematic ? 1u : 0u);
            mix(body->lockRotation ? 1u : 0u);
            mix(body->lockPositionX ? 1u : 0u);
            mix(body->lockPositionY ? 1u : 0u);
            mix(body->lockPositionZ ? 1u : 0u);
            mix(body->lockRotationX ? 1u : 0u);
            mix(body->lockRotationY ? 1u : 0u);
            mix(body->lockRotationZ ? 1u : 0u);
            mix(body->continuousCollision ? 1u : 0u);
            mix(body->allowSleeping ? 1u : 0u);
            mix(collider->isTrigger ? 1u : 0u);
            mix(static_cast<uint64_t>(std::hash<float>{}(collider->halfExtents.x)));
            mix(static_cast<uint64_t>(std::hash<float>{}(collider->halfExtents.y)));
            mix(static_cast<uint64_t>(std::hash<float>{}(collider->halfExtents.z)));
            mix(static_cast<uint64_t>(std::hash<float>{}(collider->offset.x)));
            mix(static_cast<uint64_t>(std::hash<float>{}(collider->offset.y)));
            mix(static_cast<uint64_t>(std::hash<float>{}(collider->offset.z)));
            mix(static_cast<uint64_t>(std::hash<float>{}(transform->scale.x)));
            mix(static_cast<uint64_t>(std::hash<float>{}(transform->scale.y)));
            mix(static_cast<uint64_t>(std::hash<float>{}(transform->scale.z)));
        }

        return hash;
    }

    void rebuild(Scene& scene)
    {
        entityToBody.clear();
        resetSystem();

        JPH::BodyInterface& bodies = system->GetBodyInterface();
        for (auto [id, body] : scene.view<RigidBodyComponent>()) {
            if (!body || !body->simulatePhysics)
                continue;

            const auto* collider = scene.getComponent<BoxColliderComponent>(id);
            const auto* transform = scene.getComponent<TransformComponent>(id);
            if (!collider || !transform)
                continue;

            const glm::vec3 halfExtents = glm::max(glm::abs(transform->scale) * collider->halfExtents,
                                                   glm::vec3(0.001f));
            JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(toJolt(halfExtents));
            if (glm::length2(collider->offset) > 1e-8f)
                shape = new JPH::OffsetCenterOfMassShape(shape.GetPtr(), toJolt(-collider->offset));

            JPH::BodyCreationSettings settings(
                shape.GetPtr(),
                toJoltR(transform->translation),
                toJolt(rotationQuat(*transform)),
                toMotionType(*body),
                body->type == BodyType::Static ? Layers::Static : Layers::Moving);

            settings.mUserData = id;
            settings.mFriction = std::clamp(collider->friction, 0.0f, 1.0f);
            settings.mRestitution = std::clamp(collider->restitution, 0.0f, 1.0f);
            settings.mLinearVelocity = toJolt(body->linearVelocity);
            settings.mAngularVelocity = toJolt(body->angularVelocity);
            settings.mLinearDamping = std::max(body->linearDamping, 0.0f);
            settings.mAngularDamping = std::max(body->angularDamping, 0.0f);
            settings.mGravityFactor = body->useGravity ? body->gravityScale : 0.0f;
            settings.mIsSensor = collider->isTrigger;
            settings.mAllowSleeping = body->allowSleeping;
            settings.mMotionQuality = body->continuousCollision
                ? JPH::EMotionQuality::LinearCast
                : JPH::EMotionQuality::Discrete;
            settings.mAllowedDOFs = allowedDofs(*body);
            if (body->mass > 0.0001f && body->type == BodyType::Dynamic && !body->isKinematic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = body->mass;
            }

            JPH::BodyID bodyId = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
            entityToBody[id] = bodyId;
        }
    }

    void syncSceneToBodies(Scene& scene)
    {
        JPH::BodyInterface& bodies = system->GetBodyInterface();
        for (auto [entityId, bodyId] : entityToBody) {
            auto* body = scene.getComponent<RigidBodyComponent>(entityId);
            auto* transform = scene.getComponent<TransformComponent>(entityId);
            if (!body || !transform)
                continue;

            const bool sceneDriven = body->type == BodyType::Static ||
                                     body->type == BodyType::Kinematic ||
                                     body->isKinematic;
            if (sceneDriven) {
                bodies.SetPositionAndRotationWhenChanged(
                    bodyId,
                    toJoltR(transform->translation),
                    toJolt(rotationQuat(*transform)),
                    JPH::EActivation::Activate);
                bodies.SetLinearVelocity(bodyId, toJolt(body->linearVelocity));
                bodies.SetAngularVelocity(bodyId, toJolt(body->angularVelocity));
            }
        }
    }

    void syncBodiesToScene(Scene& scene)
    {
        JPH::BodyInterface& bodies = system->GetBodyInterface();
        for (auto [entityId, bodyId] : entityToBody) {
            auto* body = scene.getComponent<RigidBodyComponent>(entityId);
            auto* transform = scene.getComponent<TransformComponent>(entityId);
            if (!body || !transform)
                continue;

            JPH::RVec3 position;
            JPH::Quat rotation;
            bodies.GetPositionAndRotation(bodyId, position, rotation);
            transform->translation = fromJoltRVec3(position);
            transform->rotation = glm::degrees(glm::eulerAngles(fromJolt(rotation)));
            body->linearVelocity = fromJoltVec3(bodies.GetLinearVelocity(bodyId));
            body->angularVelocity = fromJoltVec3(bodies.GetAngularVelocity(bodyId));
        }
    }

    JoltRegistrationGuard registration;
    JPH::TempAllocatorImpl tempAllocator;
    JPH::JobSystemThreadPool jobSystem;
    BroadPhaseLayerInterface broadPhaseLayers;
    ObjectVsBroadPhaseLayerFilter objectVsBroadPhase;
    ObjectLayerPairFilter objectLayerPairs;
    ContactCollector contacts;
    std::unique_ptr<JPH::PhysicsSystem> system;
    std::unordered_map<EntityID, JPH::BodyID> entityToBody;
    uint64_t signature = 0;
};
#endif

PhysicsWorld::PhysicsWorld()
{
}

PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::isJoltAvailable() const
{
#ifdef DEMON_USE_JOLT
    return true;
#else
    return false;
#endif
}

void PhysicsWorld::clear()
{
#ifdef DEMON_USE_JOLT
    if (m_impl)
        m_impl->clear();
#endif
}

bool PhysicsWorld::step(Scene& scene, float dt, CollisionSystem& collisions)
{
#ifdef DEMON_USE_JOLT
    if (!m_impl)
        m_impl = std::make_unique<Impl>();
    return m_impl && m_impl->step(scene, dt, collisions);
#else
    (void)scene;
    (void)dt;
    (void)collisions;
    return false;
#endif
}

} // namespace Demon
