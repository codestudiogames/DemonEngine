// ==============================================================================
//  DemonEngine::Scene  –  Implementation
//
//  Optimisations vs original:
//   - MaterialComponent changes are synced to the cached Material object only
//     when the component is actually dirty (via a per-entity dirty flag).
//     This eliminates the per-frame string comparisons and the unconditional
//     mat->uploaded = false that caused a GPU stall every single frame.
//   - Material objects for entities are created once and reused; they are only
//     invalidated when the component genuinely changes.
//   - Mesh cache and missing-mesh set are moved to Scene member state so
//     they survive across frames without being recreated as local statics.
// ==============================================================================
#include "Scene.h"
#include "renderer/Renderer.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "scripting/ScriptEngine.h"

namespace Demon {

namespace {

constexpr float kTau = 6.28318530718f;

std::string makeMeshCacheKey(const MeshRendererComponent& mr)
{
    return mr.preserveHierarchy ? std::format("{}|hier", mr.meshPath) : mr.meshPath;
}

glm::mat4 composeBoneTransform(const BoneTransform& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) *
           glm::toMat4(glm::normalize(transform.rotation)) *
           glm::scale(glm::mat4(1.0f), transform.scale);
}

glm::vec3 sampleVec3Keys(const std::vector<AnimationVec3Key>& keys, float time, const glm::vec3& fallback)
{
    if (keys.empty())
        return fallback;
    if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;

    for (size_t index = 1; index < keys.size(); ++index) {
        if (time > keys[index].time)
            continue;
        const AnimationVec3Key& a = keys[index - 1];
        const AnimationVec3Key& b = keys[index];
        const float span = std::max(b.time - a.time, 1e-5f);
        const float t = std::clamp((time - a.time) / span, 0.0f, 1.0f);
        return glm::mix(a.value, b.value, t);
    }

    return keys.back().value;
}

glm::quat sampleQuatKeys(const std::vector<AnimationQuatKey>& keys, float time, const glm::quat& fallback)
{
    if (keys.empty())
        return fallback;
    if (keys.size() == 1 || time <= keys.front().time)
        return glm::normalize(keys.front().value);
    if (time >= keys.back().time)
        return glm::normalize(keys.back().value);

    for (size_t index = 1; index < keys.size(); ++index) {
        if (time > keys[index].time)
            continue;
        const AnimationQuatKey& a = keys[index - 1];
        const AnimationQuatKey& b = keys[index];
        const float span = std::max(b.time - a.time, 1e-5f);
        const float t = std::clamp((time - a.time) / span, 0.0f, 1.0f);
        return glm::normalize(glm::slerp(a.value, b.value, t));
    }

    return glm::normalize(keys.back().value);
}

BoneTransform sampleChannelTransform(const Bone& bone, const AnimationChannel* channel, float time)
{
    BoneTransform transform = bone.bindTransform;
    if (!channel)
        return transform;

    transform.translation = sampleVec3Keys(channel->positions, time, bone.bindTransform.translation);
    transform.rotation = sampleQuatKeys(channel->rotations, time, bone.bindTransform.rotation);
    transform.scale = sampleVec3Keys(channel->scales, time, bone.bindTransform.scale);
    return transform;
}

BoneTransform blendBoneTransform(const BoneTransform& a, const BoneTransform& b, float t)
{
    BoneTransform blended{};
    blended.translation = glm::mix(a.translation, b.translation, t);
    blended.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, t));
    blended.scale = glm::mix(a.scale, b.scale, t);
    return blended;
}

float advanceAnimationTime(float time, float duration, float dt, float speed, bool looping)
{
    if (duration <= 0.0f)
        return 0.0f;

    const float nextTime = time + dt * speed;
    if (looping) {
        float wrapped = std::fmod(nextTime, duration);
        if (wrapped < 0.0f)
            wrapped += duration;
        return wrapped;
    }
    return std::clamp(nextTime, 0.0f, duration);
}

void evaluateClipPose(const Mesh& mesh, const AnimationClip& clip, float time, std::vector<BoneTransform>& outPose)
{
    const Skeleton& skeleton = mesh.getSkeleton();
    outPose.resize(skeleton.bones.size());
    std::vector<const AnimationChannel*> channels(skeleton.bones.size(), nullptr);
    for (const AnimationChannel& channel : clip.channels) {
        if (channel.boneIndex >= 0 && channel.boneIndex < static_cast<int32_t>(channels.size()))
            channels[static_cast<size_t>(channel.boneIndex)] = &channel;
    }

    for (size_t boneIndex = 0; boneIndex < skeleton.bones.size(); ++boneIndex)
        outPose[boneIndex] = sampleChannelTransform(skeleton.bones[boneIndex], channels[boneIndex], time);
}

void buildFinalBoneMatrices(const Skeleton& skeleton,
                            const std::vector<BoneTransform>& localPose,
                            std::vector<glm::mat4>& outMatrices)
{
    const size_t boneCount = skeleton.bones.size();
    outMatrices.resize(boneCount, glm::mat4(1.0f));
    std::vector<glm::mat4> globalPose(boneCount, glm::mat4(1.0f));

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const glm::mat4 local = composeBoneTransform(localPose[boneIndex]);
        const int32_t parentIndex = skeleton.bones[boneIndex].parentIndex;
        globalPose[boneIndex] = (parentIndex >= 0 && parentIndex < static_cast<int32_t>(boneCount))
            ? (globalPose[static_cast<size_t>(parentIndex)] * local)
            : local;
        outMatrices[boneIndex] = globalPose[boneIndex] * skeleton.bones[boneIndex].inverseBindPose;
    }
}

std::shared_ptr<Mesh> findAnimatedMeshInHierarchy(Scene& scene, EntityID id)
{
    if (const auto* meshRenderer = scene.getComponent<MeshRendererComponent>(id)) {
        std::shared_ptr<Mesh> mesh = scene.getResolvedMesh(id);
        if (mesh && mesh->hasSkeleton() && mesh->hasAnimations())
            return mesh;
    }

    for (EntityID child : scene.getChildren(id)) {
        if (std::shared_ptr<Mesh> mesh = findAnimatedMeshInHierarchy(scene, child))
            return mesh;
    }

    return {};
}

AnimatorComponent* findAnimatorInAncestors(Scene& scene, EntityID id)
{
    EntityID current = id;
    while (current != NULL_ENTITY) {
        if (AnimatorComponent* animator = scene.getComponent<AnimatorComponent>(current))
            return animator;
        current = scene.getParent(current);
    }
    return nullptr;
}

bool rayIntersectsAabb(const glm::vec3& origin,
                       const glm::vec3& direction,
                       const glm::vec3& minBounds,
                       const glm::vec3& maxBounds,
                       float& outDistance)
{
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float rayOrigin = origin[axis];
        const float rayDir = direction[axis];
        const float boxMin = minBounds[axis];
        const float boxMax = maxBounds[axis];

        if (std::abs(rayDir) < 1e-6f) {
            if (rayOrigin < boxMin || rayOrigin > boxMax)
                return false;
            continue;
        }

        const float invDir = 1.0f / rayDir;
        float t1 = (boxMin - rayOrigin) * invDir;
        float t2 = (boxMax - rayOrigin) * invDir;
        if (t1 > t2)
            std::swap(t1, t2);

        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMax < tMin)
            return false;
    }

    outDistance = tMin;
    return true;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

bool barycentricAtXZ(const glm::vec3& a,
                     const glm::vec3& b,
                     const glm::vec3& c,
                     float x,
                     float z,
                     float& wa,
                     float& wb,
                     float& wc)
{
    const glm::vec2 av{a.x, a.z};
    const glm::vec2 bv{b.x, b.z};
    const glm::vec2 cv{c.x, c.z};
    const glm::vec2 p{x, z};
    const glm::vec2 v0 = bv - av;
    const glm::vec2 v1 = cv - av;
    const glm::vec2 v2 = p - av;
    const float denom = v0.x * v1.y - v1.x * v0.y;
    if (std::abs(denom) < 1e-7f)
        return false;

    wb = (v2.x * v1.y - v1.x * v2.y) / denom;
    wc = (v0.x * v2.y - v2.x * v0.y) / denom;
    wa = 1.0f - wb - wc;

    constexpr float kEpsilon = -0.0005f;
    return wa >= kEpsilon && wb >= kEpsilon && wc >= kEpsilon;
}

uint32_t sanitizeGridResolution(uint32_t value, uint32_t fallback);

glm::vec2 resolveWaterDirection(const WaterBodyComponent& water)
{
    glm::vec2 direction = water.flowDirection;
    if (glm::length2(direction) < 1e-6f)
        direction = {1.0f, 0.0f};
    return glm::normalize(direction);
}

void sanitizeWaterBody(WaterBodyComponent& water)
{
    water.resolution = sanitizeGridResolution(water.resolution, 33);
    water.size = glm::max(water.size, glm::vec2(1.0f, 1.0f));
    water.depth = std::max(water.depth, 0.1f);
    water.transparency = clamp01(water.transparency);
    water.waveLength = std::max(water.waveLength, 0.1f);
    water.waveSpeed = std::max(water.waveSpeed, 0.0f);
    water.flowSpeed = std::max(water.flowSpeed, 0.0f);
    water.choppiness = std::clamp(water.choppiness, 0.0f, 2.0f);
    water.roughness = std::clamp(water.roughness, 0.02f, 1.0f);
    water.foamIntensity = std::clamp(water.foamIntensity, 0.0f, 2.0f);
    water.edgeFade = std::clamp(water.edgeFade, 0.0f, 0.45f);
}

uint32_t sanitizeGridResolution(uint32_t value, uint32_t fallback = 2)
{
    return std::clamp(value == 0 ? fallback : value, 2u, 257u);
}

void ensureTerrainData(TerrainComponent& terrain)
{
    terrain.resolution = sanitizeGridResolution(terrain.resolution, 65);
    const size_t expectedSize =
        static_cast<size_t>(terrain.resolution) * static_cast<size_t>(terrain.resolution);
    if (terrain.heights.size() != expectedSize)
        terrain.heights.assign(expectedSize, 0.0f);
    terrain.sizeX = std::max(terrain.sizeX, 1.0f);
    terrain.sizeZ = std::max(terrain.sizeZ, 1.0f);
    terrain.maxHeight = std::max(terrain.maxHeight, 0.1f);
    terrain.uvScale = std::max(terrain.uvScale, 0.1f);
}

size_t terrainIndex(const TerrainComponent& terrain, uint32_t x, uint32_t z)
{
    return static_cast<size_t>(z) * static_cast<size_t>(terrain.resolution) + x;
}

float sampleTerrainHeightNormalized(const TerrainComponent& terrain, float u, float v)
{
    if (terrain.heights.empty())
        return 0.0f;

    const uint32_t resolution = sanitizeGridResolution(terrain.resolution, 65);
    const float fx = clamp01(u) * static_cast<float>(resolution - 1);
    const float fz = clamp01(v) * static_cast<float>(resolution - 1);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
    const uint32_t z0 = static_cast<uint32_t>(std::floor(fz));
    const uint32_t x1 = std::min(x0 + 1, resolution - 1);
    const uint32_t z1 = std::min(z0 + 1, resolution - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const float h00 = terrain.heights[terrainIndex(terrain, x0, z0)];
    const float h10 = terrain.heights[terrainIndex(terrain, x1, z0)];
    const float h01 = terrain.heights[terrainIndex(terrain, x0, z1)];
    const float h11 = terrain.heights[terrainIndex(terrain, x1, z1)];
    const float hx0 = std::lerp(h00, h10, tx);
    const float hx1 = std::lerp(h01, h11, tx);
    return std::lerp(hx0, hx1, tz);
}

glm::vec3 sampleTerrainNormalNormalized(const TerrainComponent& terrain, float u, float v)
{
    const uint32_t resolution = sanitizeGridResolution(terrain.resolution, 65);
    const float du = 1.0f / static_cast<float>(std::max<int>(1, static_cast<int>(resolution) - 1));
    const float dv = du;
    const float hl = sampleTerrainHeightNormalized(terrain, u - du, v);
    const float hr = sampleTerrainHeightNormalized(terrain, u + du, v);
    const float hd = sampleTerrainHeightNormalized(terrain, u, v - dv);
    const float hu = sampleTerrainHeightNormalized(terrain, u, v + dv);
    const float cellX = terrain.sizeX / static_cast<float>(std::max<int>(1, static_cast<int>(resolution) - 1));
    const float cellZ = terrain.sizeZ / static_cast<float>(std::max<int>(1, static_cast<int>(resolution) - 1));
    glm::vec3 normal{
        (hl - hr) / std::max(cellX, 1e-4f),
        2.0f,
        (hd - hu) / std::max(cellZ, 1e-4f)
    };
    if (glm::length2(normal) < 1e-6f)
        return {0.0f, 1.0f, 0.0f};
    return glm::normalize(normal);
}

glm::vec4 terrainColorForHeight(const TerrainComponent& terrain, float height)
{
    const float height01 = clamp01(height / std::max(terrain.maxHeight, 0.001f));
    if (height01 < 0.5f)
        return glm::mix(terrain.lowColor, terrain.midColor, height01 * 2.0f);
    return glm::mix(terrain.midColor, terrain.highColor, (height01 - 0.5f) * 2.0f);
}

glm::vec3 safeNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    const glm::vec3 n = glm::cross(b - a, c - a);
    if (glm::length2(n) < 1e-6f)
        return {0.0f, 1.0f, 0.0f};
    return glm::normalize(n);
}

void appendQuad(std::vector<Vertex>& vertices,
                std::vector<uint32_t>& indices,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c,
                const glm::vec3& d,
                const glm::vec4& color,
                bool doubleSided = false)
{
    const glm::vec3 normal = safeNormal(a, b, c);
    const uint32_t base = static_cast<uint32_t>(vertices.size());

    Vertex v0{}; v0.position = a; v0.normal = normal; v0.texCoord = {0.0f, 0.0f}; v0.color = color;
    Vertex v1{}; v1.position = b; v1.normal = normal; v1.texCoord = {1.0f, 0.0f}; v1.color = color;
    Vertex v2{}; v2.position = c; v2.normal = normal; v2.texCoord = {1.0f, 1.0f}; v2.color = color;
    Vertex v3{}; v3.position = d; v3.normal = normal; v3.texCoord = {0.0f, 1.0f}; v3.color = color;

    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});

    if (!doubleSided)
        return;

    const uint32_t backBase = static_cast<uint32_t>(vertices.size());
    v0.normal = -normal;
    v1.normal = -normal;
    v2.normal = -normal;
    v3.normal = -normal;
    vertices.push_back(v0);
    vertices.push_back(v3);
    vertices.push_back(v2);
    vertices.push_back(v1);
    indices.insert(indices.end(), {backBase, backBase + 1, backBase + 2,
                                   backBase, backBase + 2, backBase + 3});
}

void appendBox(std::vector<Vertex>& vertices,
               std::vector<uint32_t>& indices,
               const glm::vec3& center,
               const glm::vec3& halfSize,
               const glm::vec4& color)
{
    const glm::vec3 p000 = center + glm::vec3(-halfSize.x, -halfSize.y, -halfSize.z);
    const glm::vec3 p001 = center + glm::vec3(-halfSize.x, -halfSize.y,  halfSize.z);
    const glm::vec3 p010 = center + glm::vec3(-halfSize.x,  halfSize.y, -halfSize.z);
    const glm::vec3 p011 = center + glm::vec3(-halfSize.x,  halfSize.y,  halfSize.z);
    const glm::vec3 p100 = center + glm::vec3( halfSize.x, -halfSize.y, -halfSize.z);
    const glm::vec3 p101 = center + glm::vec3( halfSize.x, -halfSize.y,  halfSize.z);
    const glm::vec3 p110 = center + glm::vec3( halfSize.x,  halfSize.y, -halfSize.z);
    const glm::vec3 p111 = center + glm::vec3( halfSize.x,  halfSize.y,  halfSize.z);

    appendQuad(vertices, indices, p001, p101, p111, p011, color);
    appendQuad(vertices, indices, p100, p000, p010, p110, color);
    appendQuad(vertices, indices, p101, p100, p110, p111, color);
    appendQuad(vertices, indices, p000, p001, p011, p010, color);
    appendQuad(vertices, indices, p010, p011, p111, p110, color);
    appendQuad(vertices, indices, p000, p100, p101, p001, color);
}

void appendGrassBlade(std::vector<Vertex>& vertices,
                      std::vector<uint32_t>& indices,
                      const glm::vec3& basePosition,
                      float width,
                      float height,
                      float yawRadians,
                      const glm::vec4& color)
{
    const glm::vec3 axis{std::cos(yawRadians), 0.0f, std::sin(yawRadians)};
    const glm::vec3 side = axis * (width * 0.5f);
    const glm::vec3 up{0.0f, height, 0.0f};
    appendQuad(vertices,
               indices,
               basePosition - side,
               basePosition + side,
               basePosition + up + side * 0.2f,
               basePosition + up - side * 0.2f,
               color,
               true);
}

glm::vec3 rotateAroundY(const glm::vec3& value, float yawRadians)
{
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    return {
        value.x * c - value.z * s,
        value.y,
        value.x * s + value.z * c
    };
}

std::shared_ptr<Mesh> loadFoliageSourceMesh(const std::string& path)
{
    if (path.empty())
        return {};

    static std::unordered_map<std::string, std::shared_ptr<Mesh>> s_foliageMeshCache;
    static std::unordered_set<std::string> s_missingFoliageMeshes;

    if (auto it = s_foliageMeshCache.find(path); it != s_foliageMeshCache.end())
        return it->second;
    if (s_missingFoliageMeshes.contains(path))
        return {};
    if (!std::filesystem::exists(path)) {
        s_missingFoliageMeshes.insert(path);
        DEMON_LOG_WARN("Terrain foliage source mesh missing: '{}'", path);
        return {};
    }

    auto mesh = Mesh::createFromFile(path, false);
    if (!mesh || mesh->getVertices().empty() || mesh->getIndices().empty()) {
        s_missingFoliageMeshes.insert(path);
        DEMON_LOG_WARN("Terrain foliage source mesh failed to load: '{}'", path);
        return {};
    }

    s_foliageMeshCache[path] = mesh;
    return mesh;
}

void appendMeshInstance(std::vector<Vertex>& vertices,
                        std::vector<uint32_t>& indices,
                        const Mesh& source,
                        const glm::vec3& basePosition,
                        float scale,
                        float yawRadians,
                        const glm::vec4& colorTint)
{
    const auto& sourceVertices = source.getVertices();
    const auto& sourceIndices = source.getIndices();
    if (sourceVertices.empty() || sourceIndices.empty())
        return;

    const glm::vec3 boundsMin = source.getBoundsMin();
    const glm::vec3 boundsMax = source.getBoundsMax();
    const glm::vec3 pivot{
        (boundsMin.x + boundsMax.x) * 0.5f,
        boundsMin.y,
        (boundsMin.z + boundsMax.z) * 0.5f
    };

    const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
    vertices.reserve(vertices.size() + sourceVertices.size());
    indices.reserve(indices.size() + sourceIndices.size());

    for (Vertex vertex : sourceVertices) {
        const glm::vec3 local = (vertex.position - pivot) * scale;
        vertex.position = basePosition + rotateAroundY(local, yawRadians);
        vertex.normal = glm::normalize(rotateAroundY(vertex.normal, yawRadians));
        vertex.tangent = glm::vec4(rotateAroundY(glm::vec3(vertex.tangent), yawRadians), vertex.tangent.w);
        vertex.color *= colorTint;
        vertices.push_back(vertex);
    }

    for (uint32_t index : sourceIndices)
        indices.push_back(baseIndex + index);
}

void updateProceduralMesh(std::shared_ptr<Mesh>& cachedMesh, std::shared_ptr<Mesh> rebuiltMesh)
{
    if (!rebuiltMesh) {
        cachedMesh.reset();
        return;
    }

    if (!cachedMesh) {
        cachedMesh = std::move(rebuiltMesh);
        return;
    }

    cachedMesh->setVertices(rebuiltMesh->getVertices());
    cachedMesh->setIndices(rebuiltMesh->getIndices());
    cachedMesh->setSubMeshes(rebuiltMesh->getSubMeshes());
    cachedMesh->computeBounds();
}

std::shared_ptr<Mesh> buildTerrainMesh(const TerrainComponent& terrain)
{
    const uint32_t resolution = sanitizeGridResolution(terrain.resolution, 65);
    const float invLast = 1.0f / static_cast<float>(std::max<int>(1, static_cast<int>(resolution) - 1));

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(resolution) * static_cast<size_t>(resolution));
    indices.reserve(static_cast<size_t>(resolution - 1) * static_cast<size_t>(resolution - 1) * 6);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t x = 0; x < resolution; ++x) {
            const float u = static_cast<float>(x) * invLast;
            const float v = static_cast<float>(z) * invLast;
            Vertex vertex{};
            vertex.position = {
                (u - 0.5f) * terrain.sizeX,
                terrain.heights[terrainIndex(terrain, x, z)],
                (v - 0.5f) * terrain.sizeZ
            };
            vertex.normal = sampleTerrainNormalNormalized(terrain, u, v);
            vertex.texCoord = {u * terrain.uvScale, v * terrain.uvScale};
            vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            vertex.color = terrainColorForHeight(terrain, vertex.position.y);
            vertices.push_back(vertex);
        }
    }

    for (uint32_t z = 0; z + 1 < resolution; ++z) {
        for (uint32_t x = 0; x + 1 < resolution; ++x) {
            const uint32_t i0 = z * resolution + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + resolution;
            const uint32_t i3 = i2 + 1;
            indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(vertices));
    mesh->setIndices(std::move(indices));
    SubMesh subMesh{};
    subMesh.indexCount = static_cast<uint32_t>(mesh->getIndices().size());
    mesh->addSubMesh(subMesh);
    mesh->computeBounds();
    return mesh;
}

std::shared_ptr<Mesh> buildTerrainFoliageMesh(const TerrainComponent& terrain,
                                              const TerrainFoliageComponent& foliage)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const std::shared_ptr<Mesh> treeSource = loadFoliageSourceMesh(foliage.treeMeshPath);
    const std::shared_ptr<Mesh> grassSource = loadFoliageSourceMesh(foliage.grassMeshPath);

    std::mt19937 rng(foliage.randomSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> jitter(-0.5f, 0.5f);

    auto tryPlace = [&](uint32_t desiredCount, auto&& emitFn) {
        uint32_t placed = 0;
        const uint32_t maxAttempts = std::max<uint32_t>(desiredCount * 8u, desiredCount + 1u);
        for (uint32_t attempt = 0; attempt < maxAttempts && placed < desiredCount; ++attempt) {
            float u = clamp01(unit(rng) + jitter(rng) * foliage.placementJitter / static_cast<float>(terrain.resolution));
            float v = clamp01(unit(rng) + jitter(rng) * foliage.placementJitter / static_cast<float>(terrain.resolution));
            const float localHeight = sampleTerrainHeightNormalized(terrain, u, v);
            const float height01 = clamp01(localHeight / std::max(terrain.maxHeight, 0.001f));
            if (height01 < foliage.minHeight || height01 > foliage.maxHeight)
                continue;

            const glm::vec3 normal = sampleTerrainNormalNormalized(terrain, u, v);
            const float slopeDegrees = glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)));
            if (slopeDegrees > foliage.maxSlopeDegrees)
                continue;

            const glm::vec3 position{
                (u - 0.5f) * terrain.sizeX,
                localHeight,
                (v - 0.5f) * terrain.sizeZ
            };
            emitFn(position, rng, unit);
            ++placed;
        }
    };

    auto emitTree = [&](const glm::vec3& position, float scale, float yawRadians, float variation) {
        if (treeSource) {
            appendMeshInstance(vertices, indices, *treeSource, position, scale, yawRadians, foliage.treeLeafColor);
            return;
        }

        appendBox(vertices,
                  indices,
                  position + glm::vec3(0.0f, 0.65f * scale, 0.0f),
                  {0.10f * scale, 0.65f * scale, 0.10f * scale},
                  foliage.treeTrunkColor);
        appendBox(vertices,
                  indices,
                  position + glm::vec3(0.0f, 1.70f * scale, 0.0f),
                  {0.55f * scale, 0.55f * scale, 0.55f * scale},
                  foliage.treeLeafColor);
        if (variation > 0.55f) {
            appendBox(vertices,
                      indices,
                      position + glm::vec3(0.0f, 2.25f * scale, 0.0f),
                      {0.38f * scale, 0.32f * scale, 0.38f * scale},
                      foliage.treeLeafColor);
        }
    };

    auto emitGrass = [&](const glm::vec3& position, float scale, float yawRadians) {
        if (grassSource) {
            appendMeshInstance(vertices, indices, *grassSource, position, scale, yawRadians, foliage.grassColor);
            return;
        }

        appendGrassBlade(vertices,
                         indices,
                         position,
                         0.28f * scale,
                         0.70f * scale,
                         yawRadians,
                         foliage.grassColor);
        appendGrassBlade(vertices,
                         indices,
                         position,
                         0.24f * scale,
                         0.62f * scale,
                         yawRadians + glm::half_pi<float>(),
                         foliage.grassColor);
    };

    if (foliage.treesEnabled && foliage.treeCount > 0) {
        tryPlace(foliage.treeCount, [&](const glm::vec3& position, std::mt19937& treeRng, auto& dist) {
            std::uniform_real_distribution<float> scaleDist(foliage.treeMinScale, foliage.treeMaxScale);
            std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
            const float scale = scaleDist(treeRng);
            emitTree(position, scale, angleDist(treeRng), dist(treeRng));
        });
    }

    if (foliage.grassEnabled && foliage.grassCount > 0) {
        tryPlace(foliage.grassCount, [&](const glm::vec3& position, std::mt19937& grassRng, auto&) {
            std::uniform_real_distribution<float> scaleDist(foliage.grassMinScale, foliage.grassMaxScale);
            std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
            const float scale = scaleDist(grassRng);
            emitGrass(position, scale, angleDist(grassRng));
        });
    }

    auto emitPainted = [&](const std::vector<glm::vec4>& instances, bool tree) {
        for (const glm::vec4& instance : instances) {
            const float u = clamp01(instance.x);
            const float v = clamp01(instance.y);
            const float localHeight = sampleTerrainHeightNormalized(terrain, u, v);
            const float height01 = clamp01(localHeight / std::max(terrain.maxHeight, 0.001f));
            if (height01 < foliage.minHeight || height01 > foliage.maxHeight)
                continue;

            const glm::vec3 normal = sampleTerrainNormalNormalized(terrain, u, v);
            const float slopeDegrees = glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)));
            if (slopeDegrees > foliage.maxSlopeDegrees)
                continue;

            const glm::vec3 position{
                (u - 0.5f) * terrain.sizeX,
                localHeight,
                (v - 0.5f) * terrain.sizeZ
            };
            if (tree)
                emitTree(position, std::max(instance.z, 0.01f), instance.w, 1.0f);
            else
                emitGrass(position, std::max(instance.z, 0.01f), instance.w);
        }
    };

    if (foliage.treesEnabled)
        emitPainted(foliage.paintedTrees, true);
    if (foliage.grassEnabled)
        emitPainted(foliage.paintedGrass, false);

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(vertices));
    mesh->setIndices(std::move(indices));
    SubMesh subMesh{};
    subMesh.indexCount = static_cast<uint32_t>(mesh->getIndices().size());
    mesh->addSubMesh(subMesh);
    mesh->computeBounds();
    return mesh;
}

float computeWaterWaveHeight(const WaterBodyComponent& water, float localX, float localZ, float timeSeconds)
{
    if (std::abs(water.waveAmplitude) < 1e-4f || water.waveLength <= 1e-4f)
        return 0.0f;

    const glm::vec2 direction = resolveWaterDirection(water);
    const glm::vec2 crossDir{-direction.y, direction.x};
    glm::vec2 dir1 = direction + crossDir * 0.45f;
    if (glm::length2(dir1) < 1e-6f)
        dir1 = direction;
    dir1 = glm::normalize(dir1);

    glm::vec2 dir2 = direction - crossDir * 0.65f;
    if (glm::length2(dir2) < 1e-6f)
        dir2 = direction;
    dir2 = glm::normalize(dir2);

    glm::vec2 dir3 = (-direction * 0.35f) + crossDir;
    if (glm::length2(dir3) < 1e-6f)
        dir3 = crossDir;
    dir3 = glm::normalize(dir3);

    const glm::vec2 sample{localX, localZ};
    const float choppiness = std::clamp(water.choppiness, 0.0f, 2.0f);
    auto profile = [choppiness](float phase) {
        const float s = std::sin(phase);
        const float c = std::cos(phase);
        return s + (s * std::abs(c)) * (0.45f * choppiness);
    };
    auto octave = [&](const glm::vec2& dir,
                      float amplitudeScale,
                      float wavelengthScale,
                      float speedScale,
                      float phaseOffset) {
        const float wavelength = std::max(water.waveLength * wavelengthScale, 0.1f);
        const float phase =
            (glm::dot(sample, dir) / wavelength + timeSeconds * (water.waveSpeed * speedScale + water.flowSpeed * 0.12f))
            * kTau + phaseOffset;
        return water.waveAmplitude * amplitudeScale * profile(phase);
    };

    return octave(direction, 1.00f, 1.00f, 1.00f, 0.0f)
         + octave(dir1,      0.55f, 0.58f, 1.18f, 0.9f)
         + octave(dir2,      0.32f, 0.34f, 0.82f, 1.7f)
         + octave(dir3,      0.18f, 0.21f, 1.42f, 2.4f);
}

glm::vec3 computeWaterWaveNormal(const WaterBodyComponent& water, float localX, float localZ, float timeSeconds)
{
    const float gridStepX = water.size.x / static_cast<float>(std::max<int>(1, static_cast<int>(sanitizeGridResolution(water.resolution, 33)) - 1));
    const float gridStepZ = water.size.y / static_cast<float>(std::max<int>(1, static_cast<int>(sanitizeGridResolution(water.resolution, 33)) - 1));
    const float stepX = std::max(gridStepX, water.waveLength * 0.04f);
    const float stepZ = std::max(gridStepZ, water.waveLength * 0.04f);
    const float hl = computeWaterWaveHeight(water, localX - stepX, localZ, timeSeconds);
    const float hr = computeWaterWaveHeight(water, localX + stepX, localZ, timeSeconds);
    const float hd = computeWaterWaveHeight(water, localX, localZ - stepZ, timeSeconds);
    const float hu = computeWaterWaveHeight(water, localX, localZ + stepZ, timeSeconds);
    glm::vec3 normal{
        hl - hr,
        2.0f * std::max(stepX, stepZ),
        hd - hu
    };
    if (glm::length2(normal) < 1e-6f)
        return {0.0f, 1.0f, 0.0f};
    return glm::normalize(normal);
}

std::shared_ptr<Mesh> buildWaterMesh(const WaterBodyComponent& water, float timeSeconds)
{
    (void)timeSeconds;
    const uint32_t resolution = sanitizeGridResolution(water.resolution, 33);
    const float invLast = 1.0f / static_cast<float>(std::max<int>(1, static_cast<int>(resolution) - 1));

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>(resolution) * static_cast<size_t>(resolution));
    indices.reserve(static_cast<size_t>(resolution - 1) * static_cast<size_t>(resolution - 1) * 6);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t x = 0; x < resolution; ++x) {
            const float u = static_cast<float>(x) * invLast;
            const float v = static_cast<float>(z) * invLast;
            const float localX = (u - 0.5f) * water.size.x;
            const float localZ = (v - 0.5f) * water.size.y;
            Vertex vertex{};
            vertex.position = {localX, 0.0f, localZ};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.texCoord = {u, v};
            vertex.color = water.surfaceColor;
            vertices.push_back(vertex);
        }
    }

    for (uint32_t z = 0; z + 1 < resolution; ++z) {
        for (uint32_t x = 0; x + 1 < resolution; ++x) {
            const uint32_t i0 = z * resolution + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + resolution;
            const uint32_t i3 = i2 + 1;
            indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(vertices));
    mesh->setIndices(std::move(indices));
    SubMesh subMesh{};
    subMesh.indexCount = static_cast<uint32_t>(mesh->getIndices().size());
    mesh->addSubMesh(subMesh);
    mesh->computeBounds();
    return mesh;
}

} // namespace

Scene::Scene(std::string name)
    : m_name(std::move(name)),
      m_collisionSystem(std::make_unique<CollisionSystem>()),
      m_physicsWorld(std::make_unique<PhysicsWorld>()) {}

Scene::Scene(const Scene& other)
    : m_name(other.m_name),
      m_entities(other.m_entities),
      m_pool(other.m_pool),
      m_nextID(other.m_nextID),
      m_selected(other.m_selected),
      m_simulationEnabled(false),
      m_elapsedTime(other.m_elapsedTime),
      m_meshCache(other.m_meshCache),
      m_missingMeshes(other.m_missingMeshes),
      m_materialPathCache(other.m_materialPathCache),
      m_materialEntityCache(other.m_materialEntityCache),
      m_materialDirtyFlags(other.m_materialDirtyFlags),
      m_collisionSystem(std::make_unique<CollisionSystem>()),
      m_physicsWorld(std::make_unique<PhysicsWorld>()) {}

Scene& Scene::operator=(const Scene& other)
{
    if (this == &other) return *this;
    m_name = other.m_name;
    m_entities = other.m_entities;
    m_pool = other.m_pool;
    m_nextID = other.m_nextID;
    m_selected = other.m_selected;
    m_simulationEnabled = false;
    m_elapsedTime = other.m_elapsedTime;
    m_meshCache = other.m_meshCache;
    m_missingMeshes = other.m_missingMeshes;
    m_materialPathCache = other.m_materialPathCache;
    m_materialEntityCache = other.m_materialEntityCache;
    m_materialDirtyFlags = other.m_materialDirtyFlags;
    m_terrainRenderCache.clear();
    m_foliageRenderCache.clear();
    m_waterRenderCache.clear();
    m_collisionSystem = std::make_unique<CollisionSystem>();
    m_physicsWorld = std::make_unique<PhysicsWorld>();
    return *this;
}

Entity Scene::createEntity(std::string tag) {
    return createEntityWithID(UUID{}, std::move(tag));
}

Entity Scene::createEntityWithID(UUID id, std::string tag) {
    EntityID eid = static_cast<uint64_t>(id);
    m_entities.push_back(eid);
    TagComponent tc;
    tc.tag = std::move(tag);
    m_pool.add(eid, std::move(tc));
    m_pool.add(eid, HierarchyComponent{});
    return Entity(eid, this);
}

Entity Scene::duplicateEntity(EntityID source, EntityID parentOverride)
{
    if (!entityExists(source))
        return {};

    const EntityID sourceParent = getParent(source);
    const EntityID targetParent = parentOverride != NULL_ENTITY ? parentOverride : sourceParent;
    const bool rootDuplicate = parentOverride == NULL_ENTITY;

    std::string tagName = "Entity Copy";
    if (const auto* tag = getComponent<TagComponent>(source))
        tagName = tag->tag + " Copy";

    Entity duplicate = createEntity(tagName);
    const EntityID duplicateId = duplicate.getID();
    if (targetParent != NULL_ENTITY)
        setParent(duplicateId, targetParent);

    auto copyComponent = [&]<typename T>() {
        if (const auto* component = getComponent<T>(source))
            addComponent<T>(duplicateId, *component);
    };

    copyComponent.template operator()<TransformComponent>();
    copyComponent.template operator()<MeshRendererComponent>();
    copyComponent.template operator()<AnimatorComponent>();
    copyComponent.template operator()<MaterialComponent>();
    copyComponent.template operator()<SkyboxComponent>();
    copyComponent.template operator()<FogComponent>();
    copyComponent.template operator()<VolumetricFogComponent>();
    copyComponent.template operator()<LocalVolumetricFogComponent>();
    copyComponent.template operator()<VolumetricCloudComponent>();
    copyComponent.template operator()<LensFlareComponent>();
    copyComponent.template operator()<ReflectionProbeComponent>();
    copyComponent.template operator()<IrradianceProbeVolumeComponent>();
    copyComponent.template operator()<CameraComponent>();
    copyComponent.template operator()<LightComponent>();
    copyComponent.template operator()<RigidBodyComponent>();
    copyComponent.template operator()<BoxColliderComponent>();
    copyComponent.template operator()<UIElementComponent>();
    copyComponent.template operator()<TerrainComponent>();
    copyComponent.template operator()<TerrainSculptComponent>();
    copyComponent.template operator()<TerrainFoliageComponent>();
    copyComponent.template operator()<WaterBodyComponent>();
    copyComponent.template operator()<ScriptComponent>();
    copyComponent.template operator()<AudioSourceComponent>();

    if (rootDuplicate) {
        if (auto* transform = getComponent<TransformComponent>(duplicateId))
            transform->translation += glm::vec3(0.35f, 0.0f, 0.35f);
    }

    const std::vector<EntityID> children = getChildren(source);
    for (EntityID child : children)
        duplicateEntity(child, duplicateId);

    return duplicate;
}

void Scene::destroyEntity(EntityID id) {
    if (!entityExists(id))
        return;

    std::vector<EntityID> children;
    if (const auto* hierarchy = getComponent<HierarchyComponent>(id))
        children = hierarchy->children;

    for (EntityID child : children)
        destroyEntity(child);

    const EntityID parent = getParent(id);
    if (parent != NULL_ENTITY) {
        if (auto* parentHierarchy = getComponent<HierarchyComponent>(parent))
            std::erase(parentHierarchy->children, id);
    }

    auto it = std::find(m_entities.begin(), m_entities.end(), id);
    if (it != m_entities.end())
        m_entities.erase(it);

    m_pool.removeAll(id);
    m_materialEntityCache.erase(id);
    m_materialDirtyFlags.erase(id);
    m_terrainRenderCache.erase(id);
    m_foliageRenderCache.erase(id);
    m_waterRenderCache.erase(id);
    if (m_selected == id)
        m_selected = NULL_ENTITY;
}

bool Scene::entityExists(EntityID id) const {
    return std::find(m_entities.begin(), m_entities.end(), id) != m_entities.end();
}

Entity Scene::getEntityByTag(std::string_view tag) {
    for (auto id : m_entities) {
        if (auto* tc = m_pool.get<TagComponent>(id); tc && tc->tag == tag)
            return Entity(id, this);
    }
    return {};
}

Entity Scene::getEntityByID(EntityID id) {
    if (!entityExists(id)) return {};
    return Entity(id, this);
}

bool Scene::setParent(EntityID child, EntityID parent)
{
    if (child == NULL_ENTITY || !entityExists(child))
        return false;
    if (parent != NULL_ENTITY && !entityExists(parent))
        return false;
    if (child == parent)
        return false;
    if (parent != NULL_ENTITY && isDescendantOf(parent, child))
        return false;

    const glm::mat4 worldBefore = getWorldTransform(child);
    const EntityID oldParent = getParent(child);
    if (oldParent == parent)
        return true;

    auto* childHierarchy = getComponent<HierarchyComponent>(child);
    DEMON_ASSERT(childHierarchy, "Hierarchy component missing on child entity");

    if (oldParent != NULL_ENTITY) {
        if (auto* oldParentHierarchy = getComponent<HierarchyComponent>(oldParent))
            std::erase(oldParentHierarchy->children, child);
    }

    childHierarchy->parent = parent;

    if (parent != NULL_ENTITY) {
        auto* parentHierarchy = getComponent<HierarchyComponent>(parent);
        DEMON_ASSERT(parentHierarchy, "Hierarchy component missing on parent entity");
        if (std::find(parentHierarchy->children.begin(), parentHierarchy->children.end(), child) ==
            parentHierarchy->children.end())
        {
            parentHierarchy->children.push_back(child);
        }
    }

    if (auto* transform = getComponent<TransformComponent>(child)) {
        glm::mat4 local = worldBefore;
        if (parent != NULL_ENTITY)
            local = glm::inverse(getWorldTransform(parent)) * worldBefore;
        transform->setFromMatrix(local);
    }

    return true;
}

void Scene::clearParent(EntityID child)
{
    (void)setParent(child, NULL_ENTITY);
}

EntityID Scene::getParent(EntityID id) const
{
    if (const auto* hierarchy = getComponent<HierarchyComponent>(id))
        return static_cast<EntityID>(hierarchy->parent);
    return NULL_ENTITY;
}

const std::vector<EntityID>& Scene::getChildren(EntityID id) const
{
    static const std::vector<EntityID> emptyChildren;
    if (const auto* hierarchy = getComponent<HierarchyComponent>(id))
        return hierarchy->children;
    return emptyChildren;
}

bool Scene::isDescendantOf(EntityID entity, EntityID potentialAncestor) const
{
    EntityID current = getParent(entity);
    while (current != NULL_ENTITY) {
        if (current == potentialAncestor)
            return true;
        current = getParent(current);
    }
    return false;
}

glm::mat4 Scene::getWorldTransform(EntityID id) const
{
    glm::mat4 local = glm::mat4(1.0f);
    if (const auto* transform = getComponent<TransformComponent>(id))
        local = transform->getMatrix();

    const EntityID parent = getParent(id);
    if (parent == NULL_ENTITY)
        return local;

    return getWorldTransform(parent) * local;
}

EntityID Scene::getPrimaryCameraID() const
{
    EntityID fallbackCamera = NULL_ENTITY;
    for (auto [id, camera] : view<CameraComponent>()) {
        if (!camera)
            continue;
        if (fallbackCamera == NULL_ENTITY)
            fallbackCamera = id;
        if (camera->primary)
            return id;
    }
    return fallbackCamera;
}

std::shared_ptr<Mesh> Scene::resolveMeshAsset(const MeshRendererComponent& mr)
{
    static std::shared_ptr<Mesh> s_cubeMesh   = Mesh::createCube(1.0f);
    static std::shared_ptr<Mesh> s_planeMesh  = Mesh::createPlane(10.0f, 10.0f);
    static std::shared_ptr<Mesh> s_sphereMesh = Mesh::createSphere(0.6f, 32, 32);

    if (mr.meshPath.empty() || mr.meshPath == "builtin:cube")
        return s_cubeMesh;
    if (mr.meshPath == "builtin:plane")
        return s_planeMesh;
    if (mr.meshPath == "builtin:sphere")
        return s_sphereMesh;

    const std::string cacheKey = makeMeshCacheKey(mr);
    if (auto it = m_meshCache.find(cacheKey); it != m_meshCache.end())
        return it->second;
    if (m_missingMeshes.contains(cacheKey)) {
        if (!std::filesystem::exists(mr.meshPath))
            return {};
        m_missingMeshes.erase(cacheKey);
    }
    if (!std::filesystem::exists(mr.meshPath)) {
        m_missingMeshes.insert(cacheKey);
        DEMON_LOG_WARN("Mesh missing, skipping: '{}'", mr.meshPath);
        return {};
    }

    auto mesh = Mesh::createFromFile(mr.meshPath, mr.preserveHierarchy);
    if (mesh)
        m_meshCache[cacheKey] = mesh;
    else
        m_missingMeshes.insert(cacheKey);
    return mesh;
}

std::shared_ptr<Mesh> Scene::getResolvedMesh(EntityID id)
{
    const auto* meshRenderer = getComponent<MeshRendererComponent>(id);
    if (!meshRenderer)
        return {};
    return resolveMeshAsset(*meshRenderer);
}

float Scene::sampleGroundAtWorld(float worldX,
                                 float worldZ,
                                 float probeTopY,
                                 float maxSnapDown,
                                 glm::vec3* outNormal)
{
    float bestHeight = std::numeric_limits<float>::lowest();
    glm::vec3 bestNormal{0.0f, 1.0f, 0.0f};
    bool found = false;

    const float maxDrop = std::max(maxSnapDown, 0.0f);
    auto acceptCandidate = [&](float height, glm::vec3 normal) {
        if (height > probeTopY || probeTopY - height > maxDrop)
            return;

        if (glm::length2(normal) < 1e-6f)
            normal = {0.0f, 1.0f, 0.0f};
        else
            normal = glm::normalize(normal);
        if (normal.y < 0.0f)
            normal = -normal;

        if (!found || height > bestHeight) {
            bestHeight = height;
            bestNormal = normal;
            found = true;
        }
    };

    for (auto [terrainId, terrain] : view<TerrainComponent>()) {
        if (!terrain || !terrain->collisionEnabled)
            continue;

        ensureTerrainData(*terrain);
        const auto* transform = getComponent<TransformComponent>(terrainId);
        const glm::vec3 translation = transform ? transform->translation : glm::vec3(0.0f);
        const glm::vec3 scale = transform ? glm::abs(transform->scale) : glm::vec3(1.0f);
        const float width = terrain->sizeX * std::max(scale.x, 0.001f);
        const float depth = terrain->sizeZ * std::max(scale.z, 0.001f);
        const float minX = translation.x - width * 0.5f;
        const float maxX = translation.x + width * 0.5f;
        const float minZ = translation.z - depth * 0.5f;
        const float maxZ = translation.z + depth * 0.5f;
        if (worldX < minX || worldX > maxX || worldZ < minZ || worldZ > maxZ)
            continue;

        const float u = (worldX - minX) / width;
        const float v = (worldZ - minZ) / depth;
        const float localHeight = sampleTerrainHeightNormalized(*terrain, u, v);
        const float worldHeight = translation.y + localHeight * std::max(scale.y, 0.001f);
        glm::vec3 worldNormal = sampleTerrainNormalNormalized(*terrain, u, v);
        worldNormal.x /= std::max(scale.x, 0.001f);
        worldNormal.y /= std::max(scale.y, 0.001f);
        worldNormal.z /= std::max(scale.z, 0.001f);
        acceptCandidate(worldHeight, worldNormal);
    }

    for (auto [id, mr] : view<MeshRendererComponent>()) {
        if (!mr || !mr->visible)
            continue;

        const auto mesh = resolveMeshAsset(*mr);
        if (!mesh)
            continue;

        const auto& vertices = mesh->getVertices();
        const auto& indices = mesh->getIndices();
        if (vertices.empty() || indices.size() < 3)
            continue;

        uint32_t indexOffset = 0;
        uint32_t indexEnd = static_cast<uint32_t>(indices.size());
        const auto& subMeshes = mesh->getSubMeshes();
        if (mr->subMeshIndex >= 0 && mr->subMeshIndex < static_cast<int32_t>(subMeshes.size())) {
            const SubMesh& subMesh = subMeshes[static_cast<size_t>(mr->subMeshIndex)];
            indexOffset = std::min(subMesh.indexOffset, static_cast<uint32_t>(indices.size()));
            indexEnd = std::min(indexOffset + subMesh.indexCount, static_cast<uint32_t>(indices.size()));
        }

        const glm::mat4 world = getWorldTransform(id);
        for (uint32_t i = indexOffset; i + 2 < indexEnd; i += 3) {
            const uint32_t ia = indices[i + 0];
            const uint32_t ib = indices[i + 1];
            const uint32_t ic = indices[i + 2];
            if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size())
                continue;

            const glm::vec3 a = glm::vec3(world * glm::vec4(vertices[ia].position, 1.0f));
            const glm::vec3 b = glm::vec3(world * glm::vec4(vertices[ib].position, 1.0f));
            const glm::vec3 c = glm::vec3(world * glm::vec4(vertices[ic].position, 1.0f));
            glm::vec3 normal = glm::cross(b - a, c - a);
            if (glm::length2(normal) < 1e-8f)
                continue;
            normal = glm::normalize(normal);
            if (std::abs(normal.y) < 0.12f)
                continue;

            float wa = 0.0f;
            float wb = 0.0f;
            float wc = 0.0f;
            if (!barycentricAtXZ(a, b, c, worldX, worldZ, wa, wb, wc))
                continue;

            const float height = a.y * wa + b.y * wb + c.y * wc;
            acceptCandidate(height, normal);
        }
    }

    if (outNormal)
        *outNormal = found ? bestNormal : glm::vec3(0.0f, 1.0f, 0.0f);
    return found ? bestHeight : std::numeric_limits<float>::lowest();
}

EntityID Scene::pickEntity(const glm::vec3& rayOrigin, const glm::vec3& rayDir)
{
    EntityID picked = NULL_ENTITY;
    float nearestHit = std::numeric_limits<float>::max();

    for (auto [id, mr] : view<MeshRendererComponent>()) {
        if (!mr || !mr->visible)
            continue;

        auto mesh = resolveMeshAsset(*mr);
        if (!mesh)
            continue;

        glm::vec3 localMin = mesh->getBoundsMin();
        glm::vec3 localMax = mesh->getBoundsMax();
        if (mr->subMeshIndex >= 0) {
            const auto& subMeshes = mesh->getSubMeshes();
            if (mr->subMeshIndex < static_cast<int32_t>(subMeshes.size())) {
                const auto& subMesh = subMeshes[static_cast<size_t>(mr->subMeshIndex)];
                localMin = subMesh.boundsMin;
                localMax = subMesh.boundsMax;
            }
        }
        const glm::mat4 world = getWorldTransform(id);

        std::array<glm::vec3, 8> corners = {{
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z},
        }};

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners) {
            const glm::vec3 transformed = glm::vec3(world * glm::vec4(corner, 1.0f));
            worldMin = glm::min(worldMin, transformed);
            worldMax = glm::max(worldMax, transformed);
        }

        float hitDistance = 0.0f;
        if (!rayIntersectsAabb(rayOrigin, rayDir, worldMin, worldMax, hitDistance))
            continue;

        if (hitDistance < nearestHit) {
            nearestHit = hitDistance;
            picked = id;
        }
    }

    for (auto [id, terrain] : view<TerrainComponent>()) {
        if (!terrain)
            continue;

        ensureTerrainData(*terrain);
        const glm::mat4 world = getWorldTransform(id);
        const auto [minIt, maxIt] = std::minmax_element(terrain->heights.begin(), terrain->heights.end());
        const float minHeight = (minIt != terrain->heights.end()) ? *minIt : 0.0f;
        const float maxHeight = (maxIt != terrain->heights.end()) ? *maxIt : terrain->maxHeight;
        const glm::vec3 localMin{-terrain->sizeX * 0.5f, minHeight, -terrain->sizeZ * 0.5f};
        const glm::vec3 localMax{ terrain->sizeX * 0.5f, maxHeight,  terrain->sizeZ * 0.5f};

        std::array<glm::vec3, 8> corners = {{
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z},
        }};

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners) {
            const glm::vec3 transformed = glm::vec3(world * glm::vec4(corner, 1.0f));
            worldMin = glm::min(worldMin, transformed);
            worldMax = glm::max(worldMax, transformed);
        }

        float hitDistance = 0.0f;
        if (rayIntersectsAabb(rayOrigin, rayDir, worldMin, worldMax, hitDistance) && hitDistance < nearestHit) {
            nearestHit = hitDistance;
            picked = id;
        }
    }

    for (auto [id, water] : view<WaterBodyComponent>()) {
        if (!water)
            continue;

        const glm::mat4 world = getWorldTransform(id);
        const float waveHeight = std::abs(water->waveAmplitude) * 1.5f;
        const glm::vec3 localMin{-water->size.x * 0.5f, -water->depth, -water->size.y * 0.5f};
        const glm::vec3 localMax{ water->size.x * 0.5f,  waveHeight,   water->size.y * 0.5f};

        std::array<glm::vec3, 8> corners = {{
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z},
        }};

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners) {
            const glm::vec3 transformed = glm::vec3(world * glm::vec4(corner, 1.0f));
            worldMin = glm::min(worldMin, transformed);
            worldMax = glm::max(worldMax, transformed);
        }

        float hitDistance = 0.0f;
        if (rayIntersectsAabb(rayOrigin, rayDir, worldMin, worldMax, hitDistance) && hitDistance < nearestHit) {
            nearestHit = hitDistance;
            picked = id;
        }
    }

    return picked;
}

void Scene::onUpdate(float dt)
{
    m_elapsedTime += std::max(dt, 0.0f);

    for (auto [id, animator] : view<AnimatorComponent>()) {
        if (!animator)
            continue;

        const std::shared_ptr<Mesh> mesh = findAnimatedMeshInHierarchy(*this, id);
        if (!mesh || !mesh->hasSkeleton() || !mesh->hasAnimations()) {
            animator->finalBoneMatrices.clear();
            continue;
        }

        if (animator->currentClip.empty())
            animator->currentClip = mesh->getAnimationClips().front().name;

        const AnimationClip* currentClip = mesh->findAnimationClip(animator->currentClip);
        if (!currentClip) {
            animator->finalBoneMatrices.clear();
            continue;
        }

        if (animator->playing && dt > 0.0f)
            animator->currentTime = advanceAnimationTime(animator->currentTime,
                                                        currentClip->duration,
                                                        dt,
                                                        animator->playbackSpeed,
                                                        animator->looping);

        std::vector<BoneTransform> localPose;
        evaluateClipPose(*mesh, *currentClip, animator->currentTime, localPose);

        if (!animator->nextClip.empty() && animator->nextClip != animator->currentClip) {
            if (const AnimationClip* nextClip = mesh->findAnimationClip(animator->nextClip)) {
                if (animator->playing && dt > 0.0f) {
                    animator->nextTime = advanceAnimationTime(animator->nextTime,
                                                             nextClip->duration,
                                                             dt,
                                                             animator->playbackSpeed,
                                                             animator->looping);
                    animator->blendElapsed += dt;
                }

                std::vector<BoneTransform> nextPose;
                evaluateClipPose(*mesh, *nextClip, animator->nextTime, nextPose);

                const float blendT = animator->blendDuration <= 1e-5f
                    ? 1.0f
                    : std::clamp(animator->blendElapsed / animator->blendDuration, 0.0f, 1.0f);

                for (size_t boneIndex = 0; boneIndex < localPose.size() && boneIndex < nextPose.size(); ++boneIndex)
                    localPose[boneIndex] = blendBoneTransform(localPose[boneIndex], nextPose[boneIndex], blendT);

                if (blendT >= 1.0f) {
                    animator->currentClip = animator->nextClip;
                    animator->currentTime = animator->nextTime;
                    animator->nextClip.clear();
                    animator->nextTime = 0.0f;
                    animator->blendElapsed = 0.0f;
                }
            } else {
                animator->nextClip.clear();
                animator->nextTime = 0.0f;
                animator->blendElapsed = 0.0f;
            }
        } else {
            animator->nextTime = 0.0f;
            animator->blendElapsed = 0.0f;
        }

        buildFinalBoneMatrices(mesh->getSkeleton(), localPose, animator->finalBoneMatrices);
    }

    if (m_simulationEnabled)
        updatePhysics(dt);
    else if (m_collisionSystem)
        m_collisionSystem->clear();

    ScriptEngine::get().processCollisionPairs(*this, m_collisionSystem->getPairs());
    ScriptEngine::get().updateScene(*this, dt);
}

void Scene::updatePhysics(float dt)
{
    if (!m_collisionSystem)
        return;

    if (m_physicsWorld && m_physicsWorld->step(*this, dt, *m_collisionSystem))
        return;

    if (dt <= 0.0f) {
        m_collisionSystem->update(*this);
        return;
    }

    auto sampleTerrainAtWorld = [this](float worldX, float worldZ, glm::vec3* outNormal = nullptr) -> float {
        float bestHeight = std::numeric_limits<float>::lowest();
        glm::vec3 bestNormal{0.0f, 1.0f, 0.0f};
        bool found = false;

        for (auto [terrainId, terrain] : view<TerrainComponent>()) {
            if (!terrain || !terrain->collisionEnabled)
                continue;

            ensureTerrainData(*terrain);
            const auto* transform = getComponent<TransformComponent>(terrainId);
            const glm::vec3 translation = transform ? transform->translation : glm::vec3(0.0f);
            const glm::vec3 scale = transform ? glm::abs(transform->scale) : glm::vec3(1.0f);
            const float width = terrain->sizeX * std::max(scale.x, 0.001f);
            const float depth = terrain->sizeZ * std::max(scale.z, 0.001f);
            const float minX = translation.x - width * 0.5f;
            const float maxX = translation.x + width * 0.5f;
            const float minZ = translation.z - depth * 0.5f;
            const float maxZ = translation.z + depth * 0.5f;
            if (worldX < minX || worldX > maxX || worldZ < minZ || worldZ > maxZ)
                continue;

            const float u = (worldX - minX) / width;
            const float v = (worldZ - minZ) / depth;
            const float localHeight = sampleTerrainHeightNormalized(*terrain, u, v);
            const float worldHeight = translation.y + localHeight * std::max(scale.y, 0.001f);
            glm::vec3 worldNormal = sampleTerrainNormalNormalized(*terrain, u, v);
            worldNormal.x /= std::max(scale.x, 0.001f);
            worldNormal.y /= std::max(scale.y, 0.001f);
            worldNormal.z /= std::max(scale.z, 0.001f);
            if (glm::length2(worldNormal) > 1e-6f)
                worldNormal = glm::normalize(worldNormal);
            else
                worldNormal = {0.0f, 1.0f, 0.0f};

            if (!found || worldHeight > bestHeight) {
                bestHeight = worldHeight;
                bestNormal = worldNormal;
                found = true;
            }
        }

        if (outNormal)
            *outNormal = found ? bestNormal : glm::vec3(0.0f, 1.0f, 0.0f);
        return found ? bestHeight : std::numeric_limits<float>::lowest();
    };

    auto findWaterAtWorld = [this](const glm::vec3& worldPosition,
                                   const WaterBodyComponent** outWater,
                                   float* outSurfaceHeight) -> bool {
        const WaterBodyComponent* bestWater = nullptr;
        float bestSurface = std::numeric_limits<float>::lowest();

        for (auto [waterId, water] : view<WaterBodyComponent>()) {
            if (!water || !water->affectsRigidBodies)
                continue;

            const auto* transform = getComponent<TransformComponent>(waterId);
            const glm::vec3 translation = transform ? transform->translation : glm::vec3(0.0f);
            const glm::vec3 scale = transform ? glm::abs(transform->scale) : glm::vec3(1.0f);
            const float width = std::max(water->size.x * std::max(scale.x, 0.001f), 0.001f);
            const float depth = std::max(water->size.y * std::max(scale.z, 0.001f), 0.001f);
            const float minX = translation.x - width * 0.5f;
            const float maxX = translation.x + width * 0.5f;
            const float minZ = translation.z - depth * 0.5f;
            const float maxZ = translation.z + depth * 0.5f;
            if (worldPosition.x < minX || worldPosition.x > maxX ||
                worldPosition.z < minZ || worldPosition.z > maxZ)
            {
                continue;
            }

            const float localX = (worldPosition.x - translation.x) / std::max(scale.x, 0.001f);
            const float localZ = (worldPosition.z - translation.z) / std::max(scale.z, 0.001f);
            const float surfaceHeight =
                translation.y + computeWaterWaveHeight(*water, localX, localZ, m_elapsedTime) * std::max(scale.y, 0.001f);
            const float bottomHeight = translation.y - water->depth * std::max(scale.y, 0.001f);
            if (worldPosition.y < bottomHeight)
                continue;

            if (!bestWater || surfaceHeight > bestSurface) {
                bestWater = water;
                bestSurface = surfaceHeight;
            }
        }

        if (outWater)
            *outWater = bestWater;
        if (outSurfaceHeight)
            *outSurfaceHeight = bestSurface;
        return bestWater != nullptr;
    };

    for (auto [id, rb] : view<RigidBodyComponent>()) {
        if (!rb || !rb->simulatePhysics)
            continue;

        auto* transform = getComponent<TransformComponent>(id);
        if (!transform)
            continue;

        const bool dynamicBody = rb->type == BodyType::Dynamic && !rb->isKinematic;
        const bool kinematicBody = rb->type == BodyType::Kinematic || rb->isKinematic;

        if (dynamicBody) {
            if (rb->useGravity)
                rb->linearVelocity += glm::vec3(0.0f, -9.81f * rb->gravityScale, 0.0f) * dt;

            const float linearDamping = std::max(0.0f, 1.0f - rb->linearDamping * dt * 8.0f);
            rb->linearVelocity *= linearDamping;
            transform->translation += rb->linearVelocity * dt;

            if (rb->lockRotation)
                rb->angularVelocity = {0.0f, 0.0f, 0.0f};
            else
                rb->angularVelocity *= std::max(0.0f, 1.0f - rb->angularDamping * dt * 8.0f);
        } else if (kinematicBody) {
            transform->translation += rb->linearVelocity * dt;
        }

        auto* collider = getComponent<BoxColliderComponent>(id);
        if (!collider || collider->isTrigger)
            continue;

        const glm::vec3 absScale = glm::abs(transform->scale);
        const glm::vec3 colliderHalfExtents = glm::max(absScale * collider->halfExtents, glm::vec3(0.001f));
        const glm::vec3 colliderCenter = transform->translation + collider->offset;
        const float colliderBottom = colliderCenter.y - colliderHalfExtents.y;
        const float colliderTop = colliderCenter.y + colliderHalfExtents.y;

        const WaterBodyComponent* activeWater = nullptr;
        float waterSurface = std::numeric_limits<float>::lowest();
        if (findWaterAtWorld(colliderCenter, &activeWater, &waterSurface) &&
            dynamicBody && activeWater && waterSurface > std::numeric_limits<float>::lowest())
        {
            const float submersion = clamp01((waterSurface - colliderBottom) /
                                             std::max(colliderTop - colliderBottom, 0.001f));
            if (submersion > 0.0f) {
                const float densityScale = std::max(0.15f, activeWater->fluidDensity / 1000.0f);
                rb->linearVelocity.y += 9.81f * rb->gravityScale * densityScale *
                                        activeWater->buoyancyMultiplier * submersion * dt;
                rb->linearVelocity *= std::max(0.0f, 1.0f - activeWater->drag * submersion * dt);

                glm::vec2 flow = activeWater->flowDirection;
                if (glm::length2(flow) > 1e-6f) {
                    flow = glm::normalize(flow);
                    const glm::vec2 targetVelocity = flow * activeWater->flowSpeed;
                    glm::vec2 currentVelocity{rb->linearVelocity.x, rb->linearVelocity.z};
                    const float blend = clamp01(submersion * dt * 2.5f);
                    currentVelocity = glm::mix(currentVelocity, targetVelocity, blend);
                    rb->linearVelocity.x = currentVelocity.x;
                    rb->linearVelocity.z = currentVelocity.y;
                }
            }
        }

        glm::vec3 terrainNormal{0.0f, 1.0f, 0.0f};
        const float terrainHeight = sampleTerrainAtWorld(colliderCenter.x, colliderCenter.z, &terrainNormal);
        if (terrainHeight > std::numeric_limits<float>::lowest() && colliderBottom < terrainHeight) {
            transform->translation.y += terrainHeight - colliderBottom;
            if (dynamicBody && rb->linearVelocity.y < 0.0f)
                rb->linearVelocity.y = 0.0f;

            if (dynamicBody) {
                const glm::vec3 tangentVelocity =
                    rb->linearVelocity - terrainNormal * glm::dot(rb->linearVelocity, terrainNormal);
                rb->linearVelocity = tangentVelocity * 0.96f;
            }
        }
    }

    m_collisionSystem->update(*this);

    for (const CollisionPair& pair : m_collisionSystem->getPairs()) {
        if (pair.isTrigger)
            continue;

        auto* transformA = getComponent<TransformComponent>(pair.a);
        auto* transformB = getComponent<TransformComponent>(pair.b);
        auto* colliderA = getComponent<BoxColliderComponent>(pair.a);
        auto* colliderB = getComponent<BoxColliderComponent>(pair.b);
        if (!transformA || !transformB || !colliderA || !colliderB)
            continue;

        auto* rbA = getComponent<RigidBodyComponent>(pair.a);
        auto* rbB = getComponent<RigidBodyComponent>(pair.b);
        const bool dynamicA = rbA && rbA->simulatePhysics && rbA->type == BodyType::Dynamic && !rbA->isKinematic;
        const bool dynamicB = rbB && rbB->simulatePhysics && rbB->type == BodyType::Dynamic && !rbB->isKinematic;
        if (!dynamicA && !dynamicB)
            continue;

        glm::vec3 normal = pair.manifold.normal;
        if (glm::length2(normal) < 1e-6f)
            continue;
        normal = glm::normalize(normal);

        const float penetration = std::max(pair.manifold.penetration, 0.0f);
        if (penetration > 0.0f) {
            if (dynamicA && dynamicB) {
                transformA->translation += normal * (penetration * 0.5f);
                transformB->translation -= normal * (penetration * 0.5f);
            } else if (dynamicA) {
                transformA->translation += normal * penetration;
            } else if (dynamicB) {
                transformB->translation -= normal * penetration;
            }
        }

        const float invMassA = (dynamicA && rbA->mass > 0.0001f) ? 1.0f / rbA->mass : 0.0f;
        const float invMassB = (dynamicB && rbB->mass > 0.0001f) ? 1.0f / rbB->mass : 0.0f;
        const float invMassSum = invMassA + invMassB;
        if (invMassSum <= 0.0f)
            continue;

        const glm::vec3 velocityA = rbA ? rbA->linearVelocity : glm::vec3(0.0f);
        const glm::vec3 velocityB = rbB ? rbB->linearVelocity : glm::vec3(0.0f);
        const glm::vec3 relativeVelocity = velocityA - velocityB;
        const float relativeNormalVelocity = glm::dot(relativeVelocity, normal);
        if (relativeNormalVelocity >= 0.0f)
            continue;

        const float restitution = (colliderA->restitution + colliderB->restitution) * 0.5f;
        const float impulseScalar = -(1.0f + restitution) * relativeNormalVelocity / invMassSum;
        const glm::vec3 impulse = normal * impulseScalar;

        if (dynamicA)
            rbA->linearVelocity += impulse * invMassA;
        if (dynamicB)
            rbB->linearVelocity -= impulse * invMassB;

        glm::vec3 tangent = relativeVelocity - normal * relativeNormalVelocity;
        if (glm::length2(tangent) > 1e-6f) {
            tangent = glm::normalize(tangent);
            const float tangentVelocity = glm::dot(relativeVelocity, tangent);
            const float frictionImpulseScalar = -tangentVelocity / invMassSum;
            const float frictionLimit = impulseScalar * (colliderA->friction + colliderB->friction) * 0.5f;
            const glm::vec3 frictionImpulse = tangent *
                std::clamp(frictionImpulseScalar, -frictionLimit, frictionLimit);

            if (dynamicA)
                rbA->linearVelocity += frictionImpulse * invMassA;
            if (dynamicB)
                rbB->linearVelocity -= frictionImpulse * invMassB;
        }
    }

    m_collisionSystem->update(*this);
}

void Scene::onRender(Renderer& renderer)
{
    // ── One-time statics for built-in primitives and default material ─────────
    static std::shared_ptr<Material> s_defaultMat    = Material::createDefault();

    // ── Environment: skybox + fog ────────────────────────────────────────────
    SkyboxComponent* sky = nullptr;
    FogComponent* fog = nullptr;
    VolumetricFogComponent* volumetricFog = nullptr;
    LocalVolumetricFogComponent* localVolumetricFog = nullptr;
    glm::vec3 localVolumetricFogCenter{0.0f};
    VolumetricCloudComponent* volumetricClouds = nullptr;
    LensFlareComponent* lensFlare = nullptr;
    EntityID primaryCamera = NULL_ENTITY;
    for (auto [id, cam] : view<CameraComponent>()) {
        if (cam && cam->primary) {
            primaryCamera = id;
            break;
        }
    }

    if (primaryCamera != NULL_ENTITY) {
        sky = getComponent<SkyboxComponent>(primaryCamera);
        fog = getComponent<FogComponent>(primaryCamera);
        volumetricFog = getComponent<VolumetricFogComponent>(primaryCamera);
        volumetricClouds = getComponent<VolumetricCloudComponent>(primaryCamera);
        lensFlare = getComponent<LensFlareComponent>(primaryCamera);
    }

    if (!sky) {
        for (auto [id, sb] : view<SkyboxComponent>()) {
            if (sb) {
                sky = sb;
                break;
            }
        }
    }

    if (!fog) {
        for (auto [id, fg] : view<FogComponent>()) {
            if (fg) {
                fog = fg;
                break;
            }
        }
    }

    if (!volumetricFog) {
        for (auto [id, vf] : view<VolumetricFogComponent>()) {
            if (vf) {
                volumetricFog = vf;
                break;
            }
        }
    }

    for (auto [id, vf] : view<LocalVolumetricFogComponent>()) {
        if (vf && vf->enabled) {
            localVolumetricFog = vf;
            localVolumetricFogCenter = glm::vec3(getWorldTransform(id)[3]);
            break;
        }
    }

    if (!volumetricClouds) {
        for (auto [id, clouds] : view<VolumetricCloudComponent>()) {
            if (clouds) {
                volumetricClouds = clouds;
                break;
            }
        }
    }

    if (!lensFlare) {
        for (auto [id, flare] : view<LensFlareComponent>()) {
            if (flare) {
                lensFlare = flare;
                break;
            }
        }
    }

    renderer.setSkybox(sky ? sky->texturePath : std::string{},
                       sky ? sky->intensity : 0.0f,
                       sky ? sky->enabled : false);
    if (fog) renderer.setFog(*fog);
    else {
        FogComponent none{};
        none.enabled = false;
        renderer.setFog(none);
    }
    renderer.setVolumetricFog(volumetricFog);
    renderer.setLocalVolumetricFog(localVolumetricFog, localVolumetricFogCenter);
    renderer.setVolumetricClouds(volumetricClouds);
    renderer.setLensFlare(lensFlare);

    const glm::vec3 cameraPosition = renderer.getCameraPosition();
    const ReflectionProbeComponent* activeProbe = nullptr;
    glm::vec3 activeProbeCenter{0.0f, 0.0f, 0.0f};
    int activeProbePriority = std::numeric_limits<int>::min();
    float activeProbeDistanceSq = std::numeric_limits<float>::max();
    for (auto [id, probe] : view<ReflectionProbeComponent>()) {
        if (!probe || !probe->enabled || probe->assetPath.empty())
            continue;

        const glm::vec3 probeCenter = glm::vec3(getWorldTransform(id) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const float distanceSq = glm::length2(probeCenter - cameraPosition);
        if (!activeProbe
            || probe->priority > activeProbePriority
            || (probe->priority == activeProbePriority && distanceSq < activeProbeDistanceSq))
        {
            activeProbe = probe;
            activeProbeCenter = probeCenter;
            activeProbePriority = probe->priority;
            activeProbeDistanceSq = distanceSq;
        }
    }

    if (activeProbe) {
        renderer.setReflectionProbe(activeProbe->assetPath,
                                    activeProbeCenter,
                                    true,
                                    activeProbePriority);
    } else {
        renderer.setReflectionProbe(std::string{}, glm::vec3{0.0f, 0.0f, 0.0f}, false, 0);
    }

    renderer.setSceneTime(m_elapsedTime);

    // ── Lighting: use first directional light found ───────────────────────────
    glm::vec3 lightDir   = {0.4f, -1.0f, 0.3f};
    glm::vec3 lightColor = {1.0f, 1.0f, 1.0f};
    glm::vec3 ambient    = {0.25f, 0.25f, 0.25f};
    std::vector<Renderer::LocalLight> localLights;
    localLights.reserve(16);
    bool directionalFound = false;
    bool directionalCastsShadows = true;

    for (auto [id, light] : view<LightComponent>()) {
        if (!light)
            continue;

        glm::mat4 world = getWorldTransform(id);
        const glm::vec3 lightPosition = glm::vec3(world[3]);
        glm::vec3 lightForward = glm::vec3(world * glm::vec4(0, -1, 0, 0));
        if (glm::length2(lightForward) < 1e-6f)
            lightForward = {0.0f, -1.0f, 0.0f};
        lightForward = glm::normalize(lightForward);

        if (light->type == LightType::Directional) {
            if (!directionalFound) {
                lightDir = lightForward;
                lightColor = light->color * light->intensity;
                directionalCastsShadows = light->castShadows;
                directionalFound = true;
            }
            continue;
        }

        if (localLights.size() >= 16)
            continue;

        Renderer::LocalLight gpuLight{};
        gpuLight.positionRange = glm::vec4(lightPosition, std::max(light->range, 0.01f));
        gpuLight.directionType = glm::vec4(lightForward, light->type == LightType::Spot ? 2.0f : 1.0f);
        gpuLight.colorIntensity = glm::vec4(glm::max(light->color, glm::vec3(0.0f)), std::max(light->intensity, 0.0f));
        const float inner = glm::radians(std::clamp(light->innerAngle, 0.0f, 89.0f));
        const float outer = glm::radians(std::clamp(std::max(light->outerAngle, light->innerAngle + 0.1f), 0.1f, 89.5f));
        gpuLight.spotParams = glm::vec4(std::cos(inner), std::cos(outer), light->castShadows ? 1.0f : 0.0f, 0.0f);
        localLights.push_back(gpuLight);
    }
    renderer.setSceneLighting(lightDir, lightColor, ambient, directionalCastsShadows);
    renderer.setLocalLights(localLights);

    const IrradianceProbeVolumeComponent* ddgiVolume = nullptr;
    glm::vec3 ddgiCenter{0.0f};
    for (auto [id, volume] : view<IrradianceProbeVolumeComponent>()) {
        if (volume && volume->enabled) {
            ddgiVolume = volume;
            ddgiCenter = glm::vec3(getWorldTransform(id)[3]);
            break;
        }
    }

    std::vector<Renderer::DDGIProbe> ddgiProbes;
    if (ddgiVolume) {
        const glm::ivec3 counts{
            std::clamp(static_cast<int>(std::round(ddgiVolume->probeCounts.x)), 1, 8),
            std::clamp(static_cast<int>(std::round(ddgiVolume->probeCounts.y)), 1, 8),
            std::clamp(static_cast<int>(std::round(ddgiVolume->probeCounts.z)), 1, 8)
        };
        const int totalProbeCount = counts.x * counts.y * counts.z;
        const float strideX = counts.x > 1 ? (ddgiVolume->extents.x * 2.0f) / static_cast<float>(counts.x - 1) : ddgiVolume->extents.x;
        const float strideY = counts.y > 1 ? (ddgiVolume->extents.y * 2.0f) / static_cast<float>(counts.y - 1) : ddgiVolume->extents.y;
        const float strideZ = counts.z > 1 ? (ddgiVolume->extents.z * 2.0f) / static_cast<float>(counts.z - 1) : ddgiVolume->extents.z;
        const float probeRadius = std::max(1.0f, glm::length(glm::vec3(strideX, strideY, strideZ)) * 1.2f);
        ddgiProbes.reserve(std::min(totalProbeCount, 32));

        for (int z = 0; z < counts.z && ddgiProbes.size() < 32; ++z) {
            for (int y = 0; y < counts.y && ddgiProbes.size() < 32; ++y) {
                for (int x = 0; x < counts.x && ddgiProbes.size() < 32; ++x) {
                    const glm::vec3 t{
                        counts.x > 1 ? static_cast<float>(x) / static_cast<float>(counts.x - 1) : 0.5f,
                        counts.y > 1 ? static_cast<float>(y) / static_cast<float>(counts.y - 1) : 0.5f,
                        counts.z > 1 ? static_cast<float>(z) / static_cast<float>(counts.z - 1) : 0.5f
                    };
                    const glm::vec3 local = (t - glm::vec3(0.5f)) * ddgiVolume->extents * 2.0f;
                    const glm::vec3 probePos = ddgiCenter + local;

                    glm::vec3 irradiance = ambient * ddgiVolume->skyWeight + lightColor * 0.12f;

                    // Directional (sun) indirect bounce. Previously the probe
                    // grid ignored the sun entirely, so the dominant light
                    // produced zero GI. Approximate a single ground/sky bounce:
                    // sun energy scaled by how much the sun faces downward (i.e.
                    // how much it lights surfaces the probe can see) plus a
                    // constant sky-bounce floor.
                    if (directionalCastsShadows || directionalFound) {
                        const float sunDown = std::clamp(-lightDir.y * 0.5f + 0.5f, 0.0f, 1.0f);
                        irradiance += lightColor * (0.10f + sunDown * 0.35f) * ddgiVolume->bounceWeight;
                    }

                    for (const Renderer::LocalLight& light : localLights) {
                        const glm::vec3 toLight = glm::vec3(light.positionRange) - probePos;
                        const float dist = glm::length(toLight);
                        const float range = std::max(light.positionRange.w, 0.01f);
                        if (dist >= range)
                            continue;
                        const float rangeFade = std::clamp(1.0f - dist / range, 0.0f, 1.0f);
                        const float attenuation = rangeFade * rangeFade / std::max(1.0f, dist * dist * 0.025f);
                        irradiance += glm::vec3(light.colorIntensity) * light.colorIntensity.w * attenuation * 0.18f;
                    }

                    Renderer::DDGIProbe probe{};
                    probe.positionRadius = glm::vec4(probePos, probeRadius);
                    probe.irradiance = glm::vec4(glm::max(irradiance * ddgiVolume->tint * ddgiVolume->bounceWeight, glm::vec3(0.0f)), 1.0f);
                    probe.visibility = glm::vec4(probeRadius * 0.82f,
                                                 probeRadius * probeRadius * 0.16f,
                                                 ddgiVolume->leakReduction,
                                                 0.0f);
                    ddgiProbes.push_back(probe);
                }
            }
        }
    }
    renderer.setDDGIProbes(ddgiProbes,
                           ddgiVolume ? ddgiVolume->intensity : 0.0f,
                           ddgiVolume ? ddgiVolume->normalBias : 0.0f,
                           ddgiVolume ? ddgiVolume->leakReduction : 0.0f,
                           ddgiVolume != nullptr);

    bool requiresProceduralMeshSync = false;
    for (auto [id, terrain] : view<TerrainComponent>()) {
        (void)id;
        if (!terrain)
            continue;
        if (terrain->dirty) {
            requiresProceduralMeshSync = true;
            break;
        }
        if (auto* foliage = getComponent<TerrainFoliageComponent>(id); foliage && foliage->dirty) {
            requiresProceduralMeshSync = true;
            break;
        }
    }
    if (!requiresProceduralMeshSync) {
        for (auto [id, water] : view<WaterBodyComponent>()) {
            (void)id;
            if (water && water->dirty) {
                requiresProceduralMeshSync = true;
                break;
            }
        }
    }
    if (requiresProceduralMeshSync)
        renderer.getContext().waitForGpu();

    for (auto [id, terrain] : view<TerrainComponent>()) {
        if (!terrain)
            continue;

        ensureTerrainData(*terrain);
        auto& terrainCache = m_terrainRenderCache[id];
        const bool terrainRebuilt = terrain->dirty || !terrainCache.mesh;
        if (terrainRebuilt) {
            updateProceduralMesh(terrainCache.mesh, buildTerrainMesh(*terrain));
            if (!terrainCache.material)
                terrainCache.material = std::make_shared<Material>("Terrain_" + std::to_string(id));
            terrainCache.material->setAlbedo({1.0f, 1.0f, 1.0f, 1.0f});
            terrainCache.material->setRoughness(0.96f);
            terrainCache.material->setMetallic(0.02f);
            terrainCache.material->setAlphaBlend(false);
            terrain->dirty = false;
        }

        if (terrainCache.mesh && terrainCache.material)
            renderer.submit(*terrainCache.mesh,
                            *terrainCache.material,
                            getWorldTransform(id),
                            nullptr,
                            id == m_selected,
                            id,
                            -1,
                            terrain->castShadows);

        if (auto* foliage = getComponent<TerrainFoliageComponent>(id); foliage) {
            auto& foliageCache = m_foliageRenderCache[id];
            if (terrainRebuilt || foliage->dirty || !foliageCache.mesh) {
                updateProceduralMesh(foliageCache.mesh, buildTerrainFoliageMesh(*terrain, *foliage));
                if (!foliageCache.material)
                    foliageCache.material = std::make_shared<Material>("TerrainFoliage_" + std::to_string(id));
                foliageCache.material->setAlbedo({1.0f, 1.0f, 1.0f, 1.0f});
                foliageCache.material->setRoughness(0.88f);
                foliageCache.material->setMetallic(0.0f);
                foliageCache.material->setAlphaBlend(false);
                foliage->dirty = false;
            }

            if (foliageCache.mesh && foliageCache.material &&
                !foliageCache.mesh->getVertices().empty() &&
                !foliageCache.mesh->getIndices().empty())
            {
                renderer.submit(*foliageCache.mesh,
                                *foliageCache.material,
                                getWorldTransform(id),
                                nullptr,
                                false,
                                id,
                                -1,
                                foliage->treesEnabled || foliage->grassEnabled);
            }
        }
    }

    for (auto [id, water] : view<WaterBodyComponent>()) {
        if (!water)
            continue;

        sanitizeWaterBody(*water);

        auto& waterCache = m_waterRenderCache[id];
        if (water->dirty || !waterCache.mesh) {
            updateProceduralMesh(waterCache.mesh, buildWaterMesh(*water, 0.0f));
            if (!waterCache.material)
                waterCache.material = std::make_shared<Material>("Water_" + std::to_string(id));
            waterCache.material->setAlbedo({1.0f, 1.0f, 1.0f, water->transparency});
            waterCache.material->setRoughness(water->roughness);
            waterCache.material->setMetallic(0.02f);
            waterCache.material->setEmissive({0.0f, 0.0f, 0.0f}, 0.0f);
            waterCache.material->setAlphaBlend(true);
            water->dirty = false;
        }

        if (waterCache.mesh && waterCache.material) {
            WaterRenderParams params{};
            params.deepColorAndTransparency = {water->bottomColor.r,
                                               water->bottomColor.g,
                                               water->bottomColor.b,
                                               water->transparency};
            params.waveParams = {water->waveAmplitude,
                                 water->waveLength,
                                 water->waveSpeed,
                                 water->flowSpeed};
            params.surfaceParams = {water->flowDirection.x,
                                    water->flowDirection.y,
                                    water->choppiness,
                                    water->roughness};
            params.styleParams = {water->foamIntensity,
                                  water->edgeFade,
                                  water->depth,
                                  id == m_selected ? 1.0f : 0.0f};
            renderer.submitWater(*waterCache.mesh,
                                 *waterCache.material,
                                 getWorldTransform(id),
                                 params,
                                 id);
        }
    }

    // ── Render each MeshRenderer ──────────────────────────────────────────────
    for (auto [id, mr] : view<MeshRendererComponent>()) {
        if (!mr || !mr->visible) continue;

        glm::mat4 transform = getWorldTransform(id);

        // ── Resolve mesh ──────────────────────────────────────────────────────
        std::shared_ptr<Mesh> mesh = resolveMeshAsset(*mr);

        // ── Resolve material ──────────────────────────────────────────────────
        std::shared_ptr<Material> mat = s_defaultMat;

        if (auto* mc = getComponent<MaterialComponent>(id)) {
            // Get or create the cached material for this entity
            auto& cachedMat = m_materialEntityCache[id];
            if (!cachedMat) {
                cachedMat = std::make_shared<Material>("EntityMaterial_" + std::to_string(id));
                m_materialDirtyFlags[id] = true;  // Force a sync on first creation
            }

            // Only sync MaterialComponent → Material when the component changed.
            // MaterialComponent should set its own dirty flag (bool dirty = true)
            // when any field is modified.  We check it here and clear it after sync.
            bool& entityDirty = m_materialDirtyFlags[id];
            if (mc->dirty || entityDirty) {
                cachedMat->setAlbedo(mc->albedoColor);
                cachedMat->setMetallic(mc->metallic);
                cachedMat->setRoughness(mc->roughness);
                cachedMat->setEmissive(mc->emissiveColor, mc->emissiveStrength);
                cachedMat->setDoubleSided(mc->doubleSided);
                cachedMat->setAlphaBlend(mc->alphaBlend);
                cachedMat->setAlphaCutoff(mc->alphaCutoff);
                // Texture paths — setters only set uploaded=false when path changes
                cachedMat->setAlbedoTexture(mc->albedoTexture);
                cachedMat->setNormalTexture(mc->normalTexture);
                cachedMat->setMetallicTexture(mc->metallicTexture);
                cachedMat->setEmissiveTexture(mc->emissiveTexture);
                mc->dirty    = false;
                entityDirty  = false;
            }

            mat = cachedMat;
        } else if (!mr->materialPath.empty()) {
            auto it = m_materialPathCache.find(mr->materialPath);
            if (it != m_materialPathCache.end()) {
                mat = it->second;
            } else if (std::filesystem::exists(mr->materialPath)) {
                mat = Material::createFromFile(mr->materialPath);
                if (mat) m_materialPathCache[mr->materialPath] = mat;
            }
        }

        bool highlight = (id == m_selected);
        const std::vector<glm::mat4>* skinMatrices = nullptr;
        if (mesh && mesh->hasSkeleton()) {
            if (AnimatorComponent* animator = findAnimatorInAncestors(*this, id);
                animator && animator->finalBoneMatrices.size() >= mesh->getSkeleton().bones.size())
            {
                skinMatrices = &animator->finalBoneMatrices;
            }
        }

        if (mesh)
            renderer.submit(*mesh,
                            *mat,
                            transform,
                            skinMatrices,
                            highlight,
                            id,
                            mr->subMeshIndex,
                            mr->castShadows);
    }
}

void Scene::onViewportResize(uint32_t width, uint32_t height) {
    for (auto [id, cam] : view<CameraComponent>()) {
        (void)id;
        if (cam && !cam->fixedAspect)
            cam->camera.setViewportSize(width, height);
    }
}

std::shared_ptr<Scene> Scene::create(std::string name) {
    return std::make_shared<Scene>(std::move(name));
}

std::shared_ptr<Scene> Scene::copy(const std::shared_ptr<Scene>& src) {
    if (!src) return {};
    return std::make_shared<Scene>(*src);
}

} // namespace Demon
