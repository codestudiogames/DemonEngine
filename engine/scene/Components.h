#pragma once
// ==============================================================================
//  DemonEngine::Components
//  Plain-old-data structs that define entity behaviour.
//  DemonEngine uses a simple registry (std::unordered_map based) ECS.
// ==============================================================================
#include "core/DemonPCH.h"
#include "renderer/Camera.h"

namespace Demon {

// ── Tag ───────────────────────────────────────────────────────────────────────
struct TagComponent {
    std::string tag = "Entity";
};

// ── Hierarchy ─────────────────────────────────────────────────────────────────
struct HierarchyComponent {
    uint64_t parent = 0;
    std::vector<uint64_t> children;
};

// ── Transform ─────────────────────────────────────────────────────────────────
struct TransformComponent {
    glm::vec3 translation = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation    = {0.0f, 0.0f, 0.0f};  // Euler angles in degrees
    glm::vec3 scale       = {1.0f, 1.0f, 1.0f};

    glm::mat4 getMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 r = glm::eulerAngleXYZ(glm::radians(rotation.x),
                                          glm::radians(rotation.y),
                                          glm::radians(rotation.z));
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }

    // Editor transform decomposition helper.
    void setFromMatrix(const glm::mat4& m) {
        glm::vec3 skew; glm::vec4 persp; glm::quat quat;
        glm::decompose(m, scale, quat, translation, skew, persp);
        rotation = glm::degrees(glm::eulerAngles(quat));
    }
};

// ── Mesh Renderer ─────────────────────────────────────────────────────────────
struct MeshRendererComponent {
    std::string meshPath;
    std::string materialPath;
    int32_t     subMeshIndex    = -1;
    bool        preserveHierarchy = false;
    bool        castShadows    = true;
    bool        receiveShadows = true;
    bool        visible        = true;
    // Runtime-filled by asset system
    uint64_t    meshHandle     = 0;
    uint64_t    materialHandle = 0;
};

// ── Material (per-entity override) ─────────────────────────────────────────────
struct AnimatorComponent {
    bool        playing       = true;
    bool        looping       = true;
    float       playbackSpeed = 1.0f;
    float       blendDuration = 0.2f;
    float       blendElapsed  = 0.0f;
    float       currentTime   = 0.0f;
    float       nextTime      = 0.0f;
    std::string currentClip;
    std::string nextClip;
    std::vector<glm::mat4> finalBoneMatrices;
};

struct MaterialComponent {
    std::string materialPath;
    bool        materialLoaded = false;
    bool        dirty          = true;
    glm::vec4 albedoColor     = {1, 1, 1, 1};
    float     metallic        = 0.0f;
    float     roughness       = 0.5f;
    float     ao              = 1.0f;
    float     emissiveStrength = 0.0f;
    glm::vec3 emissiveColor   = {0, 0, 0};
    bool      doubleSided     = false;
    bool      alphaBlend      = false;
    float     alphaCutoff     = 0.5f;

    // Texture paths (optional)
    std::string albedoTexture;
    std::string normalTexture;
    std::string metallicTexture;
    std::string emissiveTexture;
};

// ── Environment: Skybox (equirectangular texture) ─────────────────────────────
struct SkyboxComponent {
    bool        enabled   = true;
    std::string texturePath;
    float       intensity = 1.0f;
};

// ── Environment: Height Fog ──────────────────────────────────────────────────
struct FogComponent {
    bool      enabled      = false;
    glm::vec3 color        = {0.65f, 0.7f, 0.75f};
    float     density      = 0.02f;  // base fog density
    float     height       = 0.0f;   // fog ground height
    float     heightFalloff= 0.2f;   // how fast fog fades with height
    float     start        = 0.0f;   // distance at which fog starts
};

struct VolumetricFogComponent {
    bool      enabled       = true;
    glm::vec3 color         = {0.62f, 0.68f, 0.78f};
    float     density       = 0.008f;
    float     intensity     = 0.34f;
    float     anisotropy    = 0.32f;
    float     height        = 0.0f;
    float     heightFalloff = 0.18f;
    float     startDistance = 8.0f;
    float     maxOpacity    = 0.28f;
};

struct LocalVolumetricFogComponent {
    bool      enabled       = true;
    glm::vec3 color         = {0.58f, 0.64f, 0.74f};
    float     density       = 0.045f;
    float     intensity     = 0.55f;
    glm::vec3 extents       = {6.0f, 3.0f, 6.0f};
    float     edgeSoftness  = 0.65f;
};

enum class VolumetricCloudPreset : uint8_t {
    Clear,
    FewClouds,
    Cloudy,
    Overcast,
    Thunder,
    Sunset,
    Storm,
};

struct VolumetricCloudComponent {
    bool      enabled       = true;
    VolumetricCloudPreset preset = VolumetricCloudPreset::Cloudy;
    float     coverage      = 0.48f;
    float     density       = 0.52f;
    float     altitude      = 85.0f;
    float     thickness     = 45.0f;
    float     scale         = 0.75f;
    float     speed         = 0.035f;
    float     darkness      = 0.22f;
    glm::vec3 tint          = {1.0f, 0.98f, 0.92f};
};

struct LensFlareComponent {
    bool      enabled       = true;
    float     intensity     = 0.28f;
    float     threshold     = 0.82f;
    float     haloWidth     = 0.45f;
    float     ghostSpacing  = 0.36f;
    float     dirtIntensity = 0.0f;
    glm::vec3 tint          = {1.0f, 0.86f, 0.62f};
};

// ── Camera ────────────────────────────────────────────────────────────────────
struct ReflectionProbeComponent {
    bool        enabled   = true;
    std::string assetPath;
    int32_t     priority  = 0;
};

struct IrradianceProbeVolumeComponent {
    bool      enabled       = true;
    glm::vec3 extents       = {12.0f, 6.0f, 12.0f};
    glm::vec3 probeCounts   = {4.0f, 2.0f, 4.0f};
    glm::vec3 tint          = {1.0f, 0.96f, 0.88f};
    float     intensity     = 0.65f;
    float     skyWeight     = 0.55f;
    float     bounceWeight  = 0.35f;
    float     normalBias    = 0.35f;
    float     leakReduction = 0.62f;
    bool      dynamicUpdate = true;
};

struct CameraComponent {
    Camera      camera;
    bool        primary       = true;   // this is the scene's main camera
    bool        fixedAspect   = false;
};

// ── Light ─────────────────────────────────────────────────────────────────────
enum class LightType { Directional, Point, Spot };

struct LightComponent {
    LightType type        = LightType::Point;
    glm::vec3 color       = {1.0f, 1.0f, 1.0f};
    float     intensity   = 1.0f;
    float     range       = 10.0f;       // point / spot
    float     innerAngle  = 15.0f;       // spot (degrees)
    float     outerAngle  = 30.0f;       // spot (degrees)
    bool      castShadows = true;
    std::string cookieTexture;
    float     cookieStrength = 1.0f;
};

// ── Rigid Body ────────────────────────────────────────────────────────────────
enum class BodyType { Static, Dynamic, Kinematic };

struct RigidBodyComponent {
    BodyType  type            = BodyType::Dynamic;
    float     mass            = 1.0f;
    float     linearDamping   = 0.01f;
    float     angularDamping  = 0.05f;
    bool      useGravity      = true;
    float     gravityScale    = 1.0f;
    bool      isKinematic     = false;
    bool      simulatePhysics = true;
    bool      lockRotation    = false;
    bool      lockPositionX   = false;
    bool      lockPositionY   = false;
    bool      lockPositionZ   = false;
    bool      lockRotationX   = false;
    bool      lockRotationY   = false;
    bool      lockRotationZ   = false;
    bool      continuousCollision = false;
    bool      allowSleeping   = true;
    uint32_t  collisionLayer  = 0;
    glm::vec3 linearVelocity  = {0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity = {0.0f, 0.0f, 0.0f};
};

// ── Box Collider ─────────────────────────────────────────────────────────────
struct BoxColliderComponent {
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};
    glm::vec3 offset      = {0.0f, 0.0f, 0.0f};
    float     friction    = 0.5f;
    float     restitution = 0.3f;
    bool      isTrigger   = false;
};

enum class UIElementKind : uint8_t {
    Text2D,
    Image2D,
    Shape2D,
    Sprite2D,
    Text3D,
    Image3D,
};

enum class UIShapeKind : uint8_t {
    Rectangle,
    Circle,
    RoundedRectangle,
};

struct UIElementComponent {
    UIElementKind kind = UIElementKind::Text2D;
    UIShapeKind shape = UIShapeKind::Rectangle;
    bool visible = true;
    bool screenSpace = true;
    bool billboard = true;
    std::string text = "Text";
    std::string imagePath;
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 size = {240.0f, 80.0f};
    float fontSize = 32.0f;
    float depth = 0.0f;
};

enum class TerrainSculptTool : uint8_t {
    Raise,
    Lower,
    Flatten,
    Smooth,
    Noise,
    Terrace,
    Erode,
    Sharpen,
};

struct TerrainComponent {
    uint32_t           resolution       = 65;
    float              sizeX            = 64.0f;
    float              sizeZ            = 64.0f;
    float              maxHeight        = 18.0f;
    float              uvScale          = 8.0f;
    glm::vec4          lowColor         = {0.30f, 0.24f, 0.16f, 1.0f};
    glm::vec4          midColor         = {0.25f, 0.48f, 0.22f, 1.0f};
    glm::vec4          highColor        = {0.62f, 0.62f, 0.64f, 1.0f};
    bool               castShadows      = true;
    bool               receiveShadows   = true;
    bool               collisionEnabled = true;
    bool               dirty            = true;
    std::vector<float> heights;
};

struct TerrainSculptComponent {
    TerrainSculptTool tool          = TerrainSculptTool::Raise;
    float             brushRadius   = 4.0f;
    float             brushStrength = 1.5f;
    float             brushFalloff  = 1.5f;
    float             flattenTarget = 2.0f;
    float             noiseScale    = 0.18f;
    float             terraceSpacing = 2.0f;
    float             erosionAmount = 0.35f;
    float             sharpenAmount = 0.45f;
    glm::vec2         brushCenter   = {0.5f, 0.5f};
    bool              autoRebuild   = true;
};

struct TerrainFoliageComponent {
    bool      treesEnabled    = true;
    bool      grassEnabled    = true;
    uint32_t  treeCount       = 0;
    uint32_t  grassCount      = 0;
    std::string treeMeshPath;
    std::string grassMeshPath;
    float     treeMinScale    = 0.85f;
    float     treeMaxScale    = 1.85f;
    float     grassMinScale   = 0.35f;
    float     grassMaxScale   = 0.95f;
    float     placementJitter = 0.85f;
    float     brushRadius     = 4.0f;
    float     brushDensity    = 0.55f;
    glm::vec2 brushCenter     = {0.5f, 0.5f};
    float     minHeight       = 0.02f;
    float     maxHeight       = 0.95f;
    float     maxSlopeDegrees = 38.0f;
    uint32_t  randomSeed      = 1337;
    glm::vec4 treeTrunkColor  = {0.36f, 0.24f, 0.14f, 1.0f};
    glm::vec4 treeLeafColor   = {0.18f, 0.50f, 0.22f, 1.0f};
    glm::vec4 grassColor      = {0.24f, 0.68f, 0.28f, 1.0f};
    std::vector<glm::vec4> paintedTrees;
    std::vector<glm::vec4> paintedGrass;
    bool      dirty           = true;
};

enum class WaterBodyType : uint8_t {
    Lake,
    River,
    Ocean,
    Pool,
    CustomArea,
};

struct WaterBodyComponent {
    WaterBodyType type               = WaterBodyType::Lake;
    uint32_t      resolution         = 33;
    glm::vec2     size               = {32.0f, 32.0f};
    float         depth              = 4.0f;
    glm::vec4     surfaceColor       = {0.05f, 0.35f, 0.30f, 0.82f};
    glm::vec4     bottomColor        = {0.01f, 0.08f, 0.18f, 0.40f};
    float         transparency       = 0.78f;
    float         waveAmplitude      = 0.35f;
    float         waveLength         = 7.5f;
    float         waveSpeed          = 1.25f;
    float         choppiness         = 0.85f;
    float         roughness          = 0.06f;
    float         foamIntensity      = 0.35f;
    float         edgeFade           = 0.18f;
    glm::vec2     flowDirection      = {1.0f, 0.0f};
    float         flowSpeed          = 0.75f;
    float         fluidDensity       = 1000.0f;
    float         drag               = 1.2f;
    float         buoyancyMultiplier = 1.0f;
    bool          affectsRigidBodies = true;
    bool          dirty              = true;
};

// ── Script ────────────────────────────────────────────────────────────────────
enum class ScriptFieldType : uint8_t {
    None = 0,
    Bool,
    Int,
    Float,
    String,
    Vec3,
    Entity,
    Entity3D,
    EntityImage,
    EntityUI,
};

struct ScriptFieldValue {
    std::string     name;
    ScriptFieldType type = ScriptFieldType::None;
    bool            hidden = false;
    bool            boolValue = false;
    int64_t         intValue = 0;
    uint64_t        entityValue = 0;
    float           floatValue = 0.0f;
    glm::vec3       vec3Value = {0.0f, 0.0f, 0.0f};
    std::string     stringValue;
};

enum class ScriptAttachmentKind : uint8_t {
    DemonScriptBehavior = 0,
    DemonScriptFile,
    CppScript,
};

struct ScriptComponent {
    ScriptAttachmentKind attachmentKind = ScriptAttachmentKind::DemonScriptBehavior;
    std::string className;      // DemonScript behavior or native C++ class name.
    std::string sourcePath;     // .ds story/action file or C++ source path.
    bool        runOnStart = true;
    std::vector<ScriptFieldValue> fieldValues;
    // Runtime pointer filled by ScriptEngine/native bridge.
    void*       instance = nullptr;
};

// ── Audio Source ─────────────────────────────────────────────────────────────
struct AudioSourceComponent {
    std::string clipPath;
    float       volume   = 1.0f;
    float       pitch    = 1.0f;
    float       range    = 20.0f;
    bool        loop     = false;
    bool        playOnAwake = true;
    bool        spatial  = true;
};

// ── Skybox ────────────────────────────────────────────────────────────────────
} // namespace Demon
