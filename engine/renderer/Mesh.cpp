// ==============================================================================
//  DemonEngine::Mesh  –  Implementation
// ==============================================================================
#include "Mesh.h"
#include "core/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef DEMON_USE_ASSIMP
#   include <assimp/Importer.hpp>
#   include <assimp/postprocess.h>
#   include <assimp/scene.h>
#endif

namespace Demon {

// ── Bounding volume ───────────────────────────────────────────────────────────
void Mesh::computeBounds() const
{
    if (m_vertices.empty()) {
        m_boundsMin = m_boundsMax = m_boundsCenter = glm::vec3(0.0f);
        m_boundsRadius = 0.0f;
        m_boundsValid  = true;
        return;
    }

    m_boundsMin = m_boundsMax = m_vertices[0].position;
    for (const auto& v : m_vertices) {
        m_boundsMin = glm::min(m_boundsMin, v.position);
        m_boundsMax = glm::max(m_boundsMax, v.position);
    }
    m_boundsCenter = (m_boundsMin + m_boundsMax) * 0.5f;

    // Radius = max distance from center to any vertex
    float maxDist2 = 0.0f;
    for (const auto& v : m_vertices) {
        float d2 = glm::dot(v.position - m_boundsCenter, v.position - m_boundsCenter);
        if (d2 > maxDist2) maxDist2 = d2;
    }
    m_boundsRadius = std::sqrt(maxDist2);
    m_boundsValid  = true;
}

const AnimationClip* Mesh::findAnimationClip(std::string_view name) const
{
    if (name.empty())
        return m_animationClips.empty() ? nullptr : &m_animationClips.front();

    for (const auto& clip : m_animationClips) {
        if (clip.name == name)
            return &clip;
    }
    return m_animationClips.empty() ? nullptr : &m_animationClips.front();
}

// ── Cube ──────────────────────────────────────────────────────────────────────
std::shared_ptr<Mesh> Mesh::createCube(float size)
{
    float h = size * 0.5f;
    auto vtx = [](glm::vec3 p, glm::vec3 n, glm::vec2 uv, glm::vec4 c) {
        Vertex v;
        v.position = p; v.normal = n; v.texCoord = uv; v.color = c;
        return v;
    };
    glm::vec4 cWhite{1.0f, 1.0f, 1.0f, 1.0f};

    std::vector<Vertex> verts = {
        // +Z front
        vtx({-h,-h, h},{0,0,1},{0,0},cWhite), vtx({ h,-h, h},{0,0,1},{1,0},cWhite),
        vtx({ h, h, h},{0,0,1},{1,1},cWhite), vtx({-h, h, h},{0,0,1},{0,1},cWhite),
        // -Z back
        vtx({ h,-h,-h},{0,0,-1},{0,0},cWhite), vtx({-h,-h,-h},{0,0,-1},{1,0},cWhite),
        vtx({-h, h,-h},{0,0,-1},{1,1},cWhite), vtx({ h, h,-h},{0,0,-1},{0,1},cWhite),
        // +X right
        vtx({ h,-h, h},{1,0,0},{0,0},cWhite), vtx({ h,-h,-h},{1,0,0},{1,0},cWhite),
        vtx({ h, h,-h},{1,0,0},{1,1},cWhite), vtx({ h, h, h},{1,0,0},{0,1},cWhite),
        // -X left
        vtx({-h,-h,-h},{-1,0,0},{0,0},cWhite), vtx({-h,-h, h},{-1,0,0},{1,0},cWhite),
        vtx({-h, h, h},{-1,0,0},{1,1},cWhite), vtx({-h, h,-h},{-1,0,0},{0,1},cWhite),
        // +Y top
        vtx({-h, h, h},{0,1,0},{0,0},cWhite), vtx({ h, h, h},{0,1,0},{1,0},cWhite),
        vtx({ h, h,-h},{0,1,0},{1,1},cWhite), vtx({-h, h,-h},{0,1,0},{0,1},cWhite),
        // -Y bottom
        vtx({-h,-h,-h},{0,-1,0},{0,0},cWhite), vtx({ h,-h,-h},{0,-1,0},{1,0},cWhite),
        vtx({ h,-h, h},{0,-1,0},{1,1},cWhite), vtx({-h,-h, h},{0,-1,0},{0,1},cWhite),
    };

    std::vector<uint32_t> idxs;
    idxs.reserve(36);
    for (uint32_t f = 0; f < 6; ++f) {
        uint32_t b = f * 4;
        idxs.insert(idxs.end(), { b, b+1, b+2, b, b+2, b+3 });
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(verts));
    mesh->setIndices(std::move(idxs));
    SubMesh sm; sm.indexCount = 36;
    mesh->addSubMesh(sm);
    mesh->computeBounds();
    DEMON_LOG_TRACE("Mesh::createCube({})", size);
    return mesh;
}

// ── Sphere ────────────────────────────────────────────────────────────────────
std::shared_ptr<Mesh> Mesh::createSphere(float radius, uint32_t rings, uint32_t sectors)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> idxs;

    for (uint32_t r = 0; r <= rings; ++r) {
        float phi = glm::pi<float>() * float(r) / float(rings);
        for (uint32_t s = 0; s <= sectors; ++s) {
            float theta = 2.0f * glm::pi<float>() * float(s) / float(sectors);
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            Vertex v;
            v.position = { radius * x, radius * y, radius * z };
            v.normal   = { x, y, z };
            v.texCoord = { float(s) / float(sectors), float(r) / float(rings) };
            verts.push_back(v);
        }
    }
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < sectors; ++s) {
            uint32_t a = r * (sectors + 1) + s;
            uint32_t b = a + sectors + 1;
            idxs.insert(idxs.end(), { a, b, a+1, b, b+1, a+1 });
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(verts));
    mesh->setIndices(std::move(idxs));
    SubMesh sm; sm.indexCount = static_cast<uint32_t>(mesh->getIndices().size());
    mesh->addSubMesh(sm);
    mesh->computeBounds();
    return mesh;
}

// ── Plane ─────────────────────────────────────────────────────────────────────
std::shared_ptr<Mesh> Mesh::createPlane(float width, float height)
{
    float hw = width * 0.5f, hh = height * 0.5f;
    glm::vec4 gridColor{0.2f, 0.2f, 0.2f, 0.0f};
    std::vector<Vertex> verts = {
        {{-hw, 0,-hh},{0,1,0},{0,0},{1,0,0,0}, gridColor},
        {{ hw, 0,-hh},{0,1,0},{1,0},{1,0,0,0}, gridColor},
        {{ hw, 0, hh},{0,1,0},{1,1},{1,0,0,0}, gridColor},
        {{-hw, 0, hh},{0,1,0},{0,1},{1,0,0,0}, gridColor},
    };
    std::vector<uint32_t> idxs = { 0,1,2, 0,2,3 };

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(verts));
    mesh->setIndices(std::move(idxs));
    SubMesh sm; sm.indexCount = 6;
    mesh->addSubMesh(sm);
    mesh->computeBounds();
    return mesh;
}

// ── Assimp loader ─────────────────────────────────────────────────────────────
static void computeSubMeshBounds(const std::vector<Vertex>& vertices,
                                 const std::vector<uint32_t>& indices,
                                 SubMesh& subMesh)
{
    if (subMesh.indexCount == 0 || subMesh.indexOffset >= indices.size()) {
        subMesh.boundsMin = {};
        subMesh.boundsMax = {};
        return;
    }

    const uint32_t firstIndex = indices[subMesh.indexOffset];
    glm::vec3 boundsMin = vertices[firstIndex].position;
    glm::vec3 boundsMax = vertices[firstIndex].position;

    const uint32_t endIndex = std::min<uint32_t>(subMesh.indexOffset + subMesh.indexCount,
                                                 static_cast<uint32_t>(indices.size()));
    for (uint32_t i = subMesh.indexOffset; i < endIndex; ++i) {
        const glm::vec3 position = vertices[indices[i]].position;
        boundsMin = glm::min(boundsMin, position);
        boundsMax = glm::max(boundsMax, position);
    }

    subMesh.boundsMin = boundsMin;
    subMesh.boundsMax = boundsMax;
}

#ifdef DEMON_USE_ASSIMP
static glm::mat4 aiToGlmMatrix(const aiMatrix4x4& matrix)
{
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1; result[1][0] = matrix.a2; result[2][0] = matrix.a3; result[3][0] = matrix.a4;
    result[0][1] = matrix.b1; result[1][1] = matrix.b2; result[2][1] = matrix.b3; result[3][1] = matrix.b4;
    result[0][2] = matrix.c1; result[1][2] = matrix.c2; result[2][2] = matrix.c3; result[3][2] = matrix.c4;
    result[0][3] = matrix.d1; result[1][3] = matrix.d2; result[2][3] = matrix.d3; result[3][3] = matrix.d4;
    return result;
}

static BoneTransform decomposeBindTransform(const glm::mat4& matrix)
{
    BoneTransform transform{};
    glm::vec3 skew{0.0f};
    glm::vec4 perspective{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 translation{0.0f};
    if (glm::decompose(matrix, scale, rotation, translation, skew, perspective)) {
        transform.translation = translation;
        transform.rotation = glm::normalize(rotation);
        transform.scale = scale;
    }
    return transform;
}

static void buildSkeletonRecursive(aiNode* node, int32_t parentIndex, Skeleton& skeleton, bool& overflowWarned)
{
    if (!node)
        return;
    if (skeleton.bones.size() >= kMaxSkinningMatrices) {
        if (!overflowWarned) {
            DEMON_LOG_WARN("Mesh importer: skeleton truncated to {} nodes.", kMaxSkinningMatrices);
            overflowWarned = true;
        }
        return;
    }

    Bone bone{};
    bone.name = node->mName.C_Str();
    bone.parentIndex = parentIndex;
    bone.bindTransform = decomposeBindTransform(aiToGlmMatrix(node->mTransformation));

    const uint32_t boneIndex = static_cast<uint32_t>(skeleton.bones.size());
    skeleton.boneNameToIndex[bone.name] = boneIndex;
    skeleton.bones.push_back(std::move(bone));

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
        buildSkeletonRecursive(node->mChildren[childIndex], static_cast<int32_t>(boneIndex), skeleton, overflowWarned);
}

static void addBoneInfluence(Vertex& vertex, uint32_t boneIndex, float weight)
{
    if (weight <= 0.0f)
        return;

    int32_t weakestSlot = 0;
    float weakestWeight = vertex.boneWeights[0];
    for (uint32_t slot = 0; slot < kMaxVertexBoneInfluences; ++slot) {
        if (vertex.boneWeights[slot] <= 0.0f) {
            vertex.boneIndices[slot] = boneIndex;
            vertex.boneWeights[slot] = weight;
            return;
        }
        if (vertex.boneWeights[slot] < weakestWeight) {
            weakestWeight = vertex.boneWeights[slot];
            weakestSlot = static_cast<int32_t>(slot);
        }
    }

    if (weight > weakestWeight) {
        vertex.boneIndices[weakestSlot] = boneIndex;
        vertex.boneWeights[weakestSlot] = weight;
    }
}

static void normalizeBoneWeights(std::vector<Vertex>& vertices)
{
    for (Vertex& vertex : vertices) {
        const float weightSum =
            vertex.boneWeights.x +
            vertex.boneWeights.y +
            vertex.boneWeights.z +
            vertex.boneWeights.w;
        if (weightSum > 1e-5f)
            vertex.boneWeights /= weightSum;
    }
}

static std::vector<AnimationClip> buildAnimationClips(const aiScene& scene, const Skeleton& skeleton)
{
    std::vector<AnimationClip> clips;
    clips.reserve(scene.mNumAnimations);

    for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex) {
        const aiAnimation* animation = scene.mAnimations[animationIndex];
        if (!animation)
            continue;

        const float ticksPerSecond = animation->mTicksPerSecond > 0.0 ? static_cast<float>(animation->mTicksPerSecond) : 25.0f;

        AnimationClip clip{};
        clip.name = animation->mName.length > 0 ? animation->mName.C_Str() : std::format("Animation {}", animationIndex + 1);
        clip.duration = ticksPerSecond > 0.0f ? static_cast<float>(animation->mDuration / ticksPerSecond) : 0.0f;
        if (clip.duration <= 0.0f)
            clip.duration = static_cast<float>(animation->mDuration / std::max(ticksPerSecond, 1.0f));
        clip.channels.reserve(animation->mNumChannels);

        for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex) {
            const aiNodeAnim* channel = animation->mChannels[channelIndex];
            if (!channel)
                continue;

            const int32_t boneIndex = skeleton.findBoneIndex(channel->mNodeName.C_Str());
            if (boneIndex < 0)
                continue;

            AnimationChannel outChannel{};
            outChannel.boneIndex = boneIndex;
            outChannel.positions.reserve(channel->mNumPositionKeys);
            outChannel.rotations.reserve(channel->mNumRotationKeys);
            outChannel.scales.reserve(channel->mNumScalingKeys);

            for (unsigned int keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex) {
                const aiVectorKey& key = channel->mPositionKeys[keyIndex];
                outChannel.positions.push_back({
                    static_cast<float>(key.mTime / ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }

            for (unsigned int keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex) {
                const aiQuatKey& key = channel->mRotationKeys[keyIndex];
                outChannel.rotations.push_back({
                    static_cast<float>(key.mTime / ticksPerSecond),
                    glm::normalize(glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z))
                });
            }

            for (unsigned int keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex) {
                const aiVectorKey& key = channel->mScalingKeys[keyIndex];
                outChannel.scales.push_back({
                    static_cast<float>(key.mTime / ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }

            clip.channels.push_back(std::move(outChannel));
        }

        clips.push_back(std::move(clip));
    }

    return clips;
}

static std::shared_ptr<Mesh> loadAssimpMesh(const std::string& path, bool preserveHierarchy)
{
    Assimp::Importer importer;
    unsigned int flags =
        aiProcess_Triangulate           |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenNormals            |
        aiProcess_CalcTangentSpace      |
        aiProcess_ImproveCacheLocality  |
        aiProcess_FlipUVs               |
        aiProcess_FixInfacingNormals;
    if (!preserveHierarchy)
        flags |= aiProcess_PreTransformVertices;

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || !scene->HasMeshes()) {
        DEMON_LOG_ERROR("Assimp failed to load '{}': {}", path, importer.GetErrorString());
        return nullptr;
    }

    Skeleton skeleton;
    if (scene->mRootNode && preserveHierarchy) {
        bool overflowWarned = false;
        buildSkeletonRecursive(scene->mRootNode, -1, skeleton, overflowWarned);
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        aiMesh* mesh = scene->mMeshes[m];
        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        uint32_t indexOffset = static_cast<uint32_t>(indices.size());

        vertices.reserve(vertices.size() + mesh->mNumVertices);
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            Vertex vert{};
            vert.color   = {1.0f, 1.0f, 1.0f, 1.0f};
            vert.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
            if (mesh->HasPositions())
                vert.position = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z };
            if (mesh->HasNormals())
                vert.normal   = { mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z };
            if (mesh->HasTextureCoords(0))
                vert.texCoord = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };
            if (mesh->HasTangentsAndBitangents())
                vert.tangent  = { mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z, 1.0f };
            if (mesh->HasVertexColors(0)) {
                const auto& c = mesh->mColors[0][v];
                vert.color = { c.r, c.g, c.b, c.a };
                if (vert.color.a < 0.01f) vert.color.a = 1.0f;
            }
            vertices.push_back(vert);
        }
        if (mesh->HasBones() && skeleton.bones.empty()) {
            DEMON_LOG_WARN("Mesh importer: '{}' has bones but was loaded without preserved hierarchy; skinning data will be incomplete.", path);
        }
        for (unsigned int boneSlot = 0; boneSlot < mesh->mNumBones; ++boneSlot) {
            aiBone* bone = mesh->mBones[boneSlot];
            if (!bone)
                continue;

            const std::string boneName = bone->mName.C_Str();
            int32_t boneIndex = skeleton.findBoneIndex(boneName);
            if (boneIndex < 0) {
                if (skeleton.bones.size() >= kMaxSkinningMatrices) {
                    DEMON_LOG_WARN("Mesh importer: skipping bone '{}' because the skeleton reached {} entries.", boneName, kMaxSkinningMatrices);
                    continue;
                }
                Bone generatedBone{};
                generatedBone.name = boneName;
                generatedBone.parentIndex = -1;
                generatedBone.inverseBindPose = aiToGlmMatrix(bone->mOffsetMatrix);
                boneIndex = static_cast<int32_t>(skeleton.bones.size());
                skeleton.boneNameToIndex[boneName] = static_cast<uint32_t>(boneIndex);
                skeleton.bones.push_back(std::move(generatedBone));
            } else {
                skeleton.bones[boneIndex].inverseBindPose = aiToGlmMatrix(bone->mOffsetMatrix);
            }

            for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                const uint32_t vertexIndex = baseVertex + weight.mVertexId;
                if (vertexIndex < vertices.size())
                    addBoneInfluence(vertices[vertexIndex], static_cast<uint32_t>(boneIndex), weight.mWeight);
            }
        }
        if (mesh->HasFaces()) {
            for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
                for (unsigned int i = 0; i < mesh->mFaces[f].mNumIndices; ++i)
                    indices.push_back(baseVertex + mesh->mFaces[f].mIndices[i]);
        }
        SubMesh sm;
        sm.indexOffset = indexOffset;
        sm.indexCount = static_cast<uint32_t>(indices.size()) - indexOffset;
        sm.materialIndex = mesh->mMaterialIndex;
        computeSubMeshBounds(vertices, indices, sm);
        subMeshes.push_back(sm);
    }

    normalizeBoneWeights(vertices);

    auto out = std::make_shared<Mesh>();
    out->setVertices(std::move(vertices));
    out->setIndices(std::move(indices));
    out->setSkeleton(std::move(skeleton));
    if (scene->HasAnimations() && out->hasSkeleton())
        out->setAnimationClips(buildAnimationClips(*scene, out->getSkeleton()));
    for (auto& sm : subMeshes) out->addSubMesh(sm);
    out->computeBounds();
    DEMON_LOG_INFO("Mesh loaded (Assimp{}): '{}' ({} verts, {} bones, {} clips)",
                   preserveHierarchy ? " hierarchy" : "",
                   path,
                   out->getVertices().size(),
                   out->getSkeleton().bones.size(),
                   out->getAnimationClips().size());
    return out;
}
#endif

// ── OBJ loader ────────────────────────────────────────────────────────────────
std::shared_ptr<Mesh> Mesh::createFromFile(const std::string& path, bool preserveHierarchy)
{
    std::filesystem::path inPath(path);
    std::string ext = inPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

#ifdef DEMON_USE_ASSIMP
    if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae") {
        auto mesh = loadAssimpMesh(path, preserveHierarchy);
        if (mesh) mesh->m_filePath = path;
        return mesh;
    }
#endif

    std::ifstream file(path);
    if (!file.is_open()) {
        DEMON_LOG_ERROR("Mesh::createFromFile: cannot open '{}'", path);
        return nullptr;
    }

    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> texCoords;
    std::vector<Vertex>    vertices;
    std::vector<uint32_t>  indices;
    std::unordered_map<std::string, uint32_t> indexMap;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token; ss >> token;

        if (token == "v") {
            glm::vec3 p; ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (token == "vn") {
            glm::vec3 n; ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (token == "vt") {
            glm::vec2 t; ss >> t.x >> t.y;
            t.y = 1.0f - t.y;  // Flip V for texture coordinate convention
            texCoords.push_back(t);
        } else if (token == "f") {
            std::vector<std::string> faceVerts;
            std::string fv;
            while (ss >> fv) faceVerts.push_back(fv);

            auto parseVert = [&](const std::string& s) -> uint32_t {
                if (auto it = indexMap.find(s); it != indexMap.end())
                    return it->second;
                Vertex v{};
                v.color   = {1.0f, 1.0f, 1.0f, 1.0f};
                v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                std::istringstream vs(s);
                std::string part;
                int slot = 0;
                while (std::getline(vs, part, '/')) {
                    if (!part.empty()) {
                        int idx = std::stoi(part) - 1;
                        if      (slot == 0 && idx >= 0 && idx < (int)positions.size())  v.position = positions[idx];
                        else if (slot == 1 && idx >= 0 && idx < (int)texCoords.size()) v.texCoord = texCoords[idx];
                        else if (slot == 2 && idx >= 0 && idx < (int)normals.size())   v.normal   = normals[idx];
                    }
                    ++slot;
                }
                uint32_t newIdx = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v);
                indexMap[s] = newIdx;
                return newIdx;
            };

            // Fan triangulation (handles tris and quads)
            for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                indices.push_back(parseVert(faceVerts[0]));
                indices.push_back(parseVert(faceVerts[i]));
                indices.push_back(parseVert(faceVerts[i + 1]));
            }
        }
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->setVertices(std::move(vertices));
    mesh->setIndices(std::move(indices));
    SubMesh sm;
    sm.indexCount = static_cast<uint32_t>(mesh->getIndices().size());
    computeSubMeshBounds(mesh->getVertices(), mesh->getIndices(), sm);
    mesh->addSubMesh(sm);
    mesh->m_filePath = path;
    mesh->computeBounds();
    DEMON_LOG_INFO("Mesh loaded: '{}' ({} verts)", path, mesh->getVertices().size());
    return mesh;
}

} // namespace Demon
