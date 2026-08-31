#pragma once
// ==============================================================================
//  DemonEngine::Mesh
//  CPU-side vertex/index data + GPU buffer handles.
//
//  Added vs original:
//   - computeBounds() calculates and caches an AABB so the renderer can do
//     tighter frustum culling than the scale-derived sphere approximation.
//   - getBoundsCenter() / getBoundsRadius() expose the cached sphere for the
//     current conservative frustum test in Renderer::submit().
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

constexpr uint32_t kMaxVertexBoneInfluences = 4;
constexpr uint32_t kMaxSkinningMatrices = 256;

struct BoneTransform {
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

struct Bone {
    std::string name;
    int32_t parentIndex = -1;
    glm::mat4 inverseBindPose{1.0f};
    BoneTransform bindTransform{};
};

struct Skeleton {
    std::vector<Bone> bones;
    std::unordered_map<std::string, uint32_t> boneNameToIndex;

    [[nodiscard]] int32_t findBoneIndex(std::string_view name) const
    {
        auto it = boneNameToIndex.find(std::string(name));
        return it == boneNameToIndex.end() ? -1 : static_cast<int32_t>(it->second);
    }
};

struct AnimationVec3Key {
    float time = 0.0f;
    glm::vec3 value{0.0f, 0.0f, 0.0f};
};

struct AnimationQuatKey {
    float time = 0.0f;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct AnimationChannel {
    int32_t boneIndex = -1;
    std::vector<AnimationVec3Key> positions;
    std::vector<AnimationQuatKey> rotations;
    std::vector<AnimationVec3Key> scales;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

struct Vertex {
    glm::vec3 position  {0, 0, 0};
    glm::vec3 normal    {0, 1, 0};
    glm::vec2 texCoord  {0, 0};
    glm::vec4 tangent   {1, 0, 0, 0};
    glm::vec4 color     {1, 1, 1, 1};
    glm::uvec4 boneIndices{0u, 0u, 0u, 0u};
    glm::vec4  boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
};

struct SubMesh {
    uint32_t indexOffset   = 0;
    uint32_t indexCount    = 0;
    uint32_t materialIndex = 0;
    glm::vec3 boundsMin    = {0.0f, 0.0f, 0.0f};
    glm::vec3 boundsMax    = {0.0f, 0.0f, 0.0f};
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    // ── Factory ───────────────────────────────────────────────────────────────
    static std::shared_ptr<Mesh> createCube(float size = 1.0f);
    static std::shared_ptr<Mesh> createSphere(float radius = 0.5f, uint32_t rings = 32, uint32_t sectors = 32);
    static std::shared_ptr<Mesh> createPlane(float width = 1.0f, float height = 1.0f);
    static std::shared_ptr<Mesh> createFromFile(const std::string& path, bool preserveHierarchy = false);

    // ── Data setters ──────────────────────────────────────────────────────────
    void setVertices(std::vector<Vertex> verts) {
        m_vertices = std::move(verts);
        invalidateUploadState();
        m_boundsValid = false;  // Invalidate cached bounds
    }
    void setIndices(std::vector<uint32_t> idxs) {
        m_indices = std::move(idxs);
        invalidateUploadState();
    }
    void setSubMeshes(std::vector<SubMesh> subMeshes) {
        m_subMeshes = std::move(subMeshes);
        invalidateUploadState();
    }
    void addSubMesh(SubMesh sm) {
        m_subMeshes.push_back(sm);
        invalidateUploadState();
    }

    // ── Data getters ──────────────────────────────────────────────────────────
    [[nodiscard]] const std::vector<Vertex>&   getVertices()  const { return m_vertices;  }
    [[nodiscard]] const std::vector<uint32_t>& getIndices()   const { return m_indices;   }
    [[nodiscard]] const std::vector<SubMesh>&  getSubMeshes() const { return m_subMeshes; }
    [[nodiscard]] const std::string&           getFilePath()  const { return m_filePath;  }
    [[nodiscard]] const Skeleton& getSkeleton() const { return m_skeleton; }
    [[nodiscard]] Skeleton& getSkeleton() { return m_skeleton; }
    [[nodiscard]] const std::vector<AnimationClip>& getAnimationClips() const { return m_animationClips; }
    [[nodiscard]] bool hasSkeleton() const { return !m_skeleton.bones.empty(); }
    [[nodiscard]] bool hasAnimations() const { return !m_animationClips.empty(); }
    void setSkeleton(Skeleton skeleton) { m_skeleton = std::move(skeleton); }
    void setAnimationClips(std::vector<AnimationClip> clips) { m_animationClips = std::move(clips); }
    [[nodiscard]] const AnimationClip* findAnimationClip(std::string_view name) const;

    // ── Bounding volume ───────────────────────────────────────────────────────
    // Lazily computed from vertex data.  Call after setVertices().
    void computeBounds() const;
    [[nodiscard]] glm::vec3 getBoundsCenter() const { ensureBounds(); return m_boundsCenter; }
    [[nodiscard]] float     getBoundsRadius() const { ensureBounds(); return m_boundsRadius; }
    [[nodiscard]] glm::vec3 getBoundsMin()    const { ensureBounds(); return m_boundsMin; }
    [[nodiscard]] glm::vec3 getBoundsMax()    const { ensureBounds(); return m_boundsMax; }

    // ── GPU handles (filled by renderer) ─────────────────────────────────────
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW  indexView{};
    bool           uploaded     = false;

private:
    void ensureBounds() const { if (!m_boundsValid) computeBounds(); }
    void invalidateUploadState() {
        vertexBuffer.Reset();
        indexBuffer.Reset();
        vertexView = {};
        indexView = {};
        uploaded = false;
    }

    std::vector<Vertex>   m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<SubMesh>  m_subMeshes;
    std::string           m_filePath;
    Skeleton              m_skeleton;
    std::vector<AnimationClip> m_animationClips;

    // Cached bounds (mutable so computeBounds() can be called from const contexts)
    mutable glm::vec3 m_boundsMin   {0.0f};
    mutable glm::vec3 m_boundsMax   {0.0f};
    mutable glm::vec3 m_boundsCenter{0.0f};
    mutable float     m_boundsRadius = 0.0f;
    mutable bool      m_boundsValid  = false;
};

} // namespace Demon
