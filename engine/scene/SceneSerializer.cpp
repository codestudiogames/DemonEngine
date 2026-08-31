// ==============================================================================
//  DemonEngine::SceneSerializer  -  JSON + Binary serialization
// ==============================================================================
#include "SceneSerializer.h"
#include "Scene.h"
#include "Components.h"
#include "core/Logger.h"
#include "serialization/Serialization.h"
#include <fstream>
#include <sstream>

namespace Demon {

namespace {

constexpr uint32_t kSceneMagic = 0x444D5343; // 'DMSC'
constexpr uint16_t kSceneVersion = 14;

enum ComponentBits : uint32_t {
    HasTransform   = 1 << 0,
    HasMesh        = 1 << 1,
    HasMaterial    = 1 << 2,
    HasCamera      = 1 << 3,
    HasLight       = 1 << 4,
    HasScript      = 1 << 5,
    HasRigidBody   = 1 << 6,
    HasBoxCollider = 1 << 7,
    HasSkybox      = 1 << 8,
    HasFog         = 1 << 9,
    HasTerrain     = 1 << 10,
    HasTerrainSculpt = 1 << 11,
    HasTerrainFoliage = 1 << 12,
    HasWaterBody   = 1 << 13,
    HasReflectionProbe = 1 << 14,
    HasAnimator    = 1 << 15,
    HasVolumetricFog = 1 << 16,
    HasVolumetricCloud = 1 << 17,
    HasLensFlare = 1 << 18,
    HasUIElement = 1 << 19,
};

struct SceneHeader {
    uint32_t magic = kSceneMagic;
    uint16_t version = kSceneVersion;
    uint16_t reserved = 0;
};

void writeVec3(JsonWriter& w, const glm::vec3& v) {
    w.beginArray();
    w.value(v.x); w.value(v.y); w.value(v.z);
    w.endArray();
}

void writeVec2(JsonWriter& w, const glm::vec2& v) {
    w.beginArray();
    w.value(v.x); w.value(v.y);
    w.endArray();
}

void writeVec4(JsonWriter& w, const glm::vec4& v) {
    w.beginArray();
    w.value(v.x); w.value(v.y); w.value(v.z); w.value(v.w);
    w.endArray();
}

void writeVec4Array(JsonWriter& w, const std::vector<glm::vec4>& values) {
    w.beginArray();
    for (const glm::vec4& v : values)
        writeVec4(w, v);
    w.endArray();
}

void writeVec2Bin(BinaryWriter& w, const glm::vec2& v) {
    w.write(v.x); w.write(v.y);
}

void writeVec3Bin(BinaryWriter& w, const glm::vec3& v) {
    w.write(v.x); w.write(v.y); w.write(v.z);
}

void writeVec4Bin(BinaryWriter& w, const glm::vec4& v) {
    w.write(v.x); w.write(v.y); w.write(v.z); w.write(v.w);
}

void readVec3Bin(BinaryReader& r, glm::vec3& v) {
    r.read(v.x); r.read(v.y); r.read(v.z);
}

void readVec2Bin(BinaryReader& r, glm::vec2& v) {
    r.read(v.x); r.read(v.y);
}

void readVec4Bin(BinaryReader& r, glm::vec4& v) {
    r.read(v.x); r.read(v.y); r.read(v.z); r.read(v.w);
}

glm::vec3 readVec3(const JsonValue& v) {
    if (!v.isArray()) return {};
    const auto& a = v.asArray();
    if (a.size() < 3) return {};
    return { static_cast<float>(a[0].asNumber()),
             static_cast<float>(a[1].asNumber()),
             static_cast<float>(a[2].asNumber()) };
}

glm::vec2 readVec2(const JsonValue& v) {
    if (!v.isArray()) return {};
    const auto& a = v.asArray();
    if (a.size() < 2) return {};
    return { static_cast<float>(a[0].asNumber()),
             static_cast<float>(a[1].asNumber()) };
}

glm::vec4 readVec4(const JsonValue& v) {
    if (!v.isArray()) return {};
    const auto& a = v.asArray();
    if (a.size() < 4) return {};
    return { static_cast<float>(a[0].asNumber()),
             static_cast<float>(a[1].asNumber()),
             static_cast<float>(a[2].asNumber()),
             static_cast<float>(a[3].asNumber()) };
}

std::vector<glm::vec4> readVec4Array(const JsonValue* value) {
    std::vector<glm::vec4> result;
    if (!value || !value->isArray())
        return result;
    result.reserve(value->asArray().size());
    for (const JsonValue& item : value->asArray()) {
        if (item.isArray())
            result.push_back(readVec4(item));
    }
    return result;
}

bool readBool(const JsonValue* v, bool fallback) {
    return v ? v->asBool(fallback) : fallback;
}

float readFloat(const JsonValue* v, float fallback) {
    return v ? static_cast<float>(v->asNumber(fallback)) : fallback;
}

int readInt(const JsonValue* v, int fallback) {
    return v ? static_cast<int>(v->asNumber(fallback)) : fallback;
}

std::string readString(const JsonValue* v, const std::string& fallback) {
    return (v && v->isString()) ? v->asString() : fallback;
}

const char* scriptFieldTypeToString(ScriptFieldType type)
{
    switch (type) {
        case ScriptFieldType::Bool:   return "bool";
        case ScriptFieldType::Int:    return "int";
        case ScriptFieldType::Float:  return "float";
        case ScriptFieldType::String: return "string";
        case ScriptFieldType::Vec3:   return "vec3";
        case ScriptFieldType::Entity: return "entity";
        case ScriptFieldType::Entity3D: return "entity3d";
        case ScriptFieldType::EntityImage: return "entity_image";
        case ScriptFieldType::EntityUI: return "entity_ui";
        default:                      return "none";
    }
}

ScriptFieldType scriptFieldTypeFromString(const JsonValue* value, ScriptFieldType fallback = ScriptFieldType::None)
{
    if (!value || !value->isString())
        return fallback;

    const std::string type = value->asString();
    if (type == "bool")   return ScriptFieldType::Bool;
    if (type == "int")    return ScriptFieldType::Int;
    if (type == "float")  return ScriptFieldType::Float;
    if (type == "string" || type == "str") return ScriptFieldType::String;
    if (type == "vec3")   return ScriptFieldType::Vec3;
    if (type == "entity") return ScriptFieldType::Entity;
    if (type == "entity3d" || type == "entity_3d") return ScriptFieldType::Entity3D;
    if (type == "entity_image" || type == "entityimg" || type == "entity_img") return ScriptFieldType::EntityImage;
    if (type == "entity_ui" || type == "entityui") return ScriptFieldType::EntityUI;
    return fallback;
}

void writeScriptFieldValue(JsonWriter& w, const ScriptFieldValue& field)
{
    w.beginObject();
    w.key("name");
    w.value(field.name);
    w.key("type");
    w.value(scriptFieldTypeToString(field.type));
    w.key("hidden");
    w.value(field.hidden);

    switch (field.type) {
        case ScriptFieldType::Bool:
            w.key("value");
            w.value(field.boolValue);
            break;
        case ScriptFieldType::Int:
            w.key("value");
            w.value(field.intValue);
            break;
        case ScriptFieldType::Float:
            w.key("value");
            w.value(field.floatValue);
            break;
        case ScriptFieldType::String:
            w.key("value");
            w.value(field.stringValue);
            break;
        case ScriptFieldType::Vec3:
            w.key("value");
            writeVec3(w, field.vec3Value);
            break;
        case ScriptFieldType::Entity:
        case ScriptFieldType::Entity3D:
        case ScriptFieldType::EntityImage:
        case ScriptFieldType::EntityUI:
            w.key("value");
            w.value(static_cast<int64_t>(field.entityValue));
            break;
        default:
            break;
    }

    w.endObject();
}

bool readScriptFieldValue(const JsonValue& value, ScriptFieldValue& out)
{
    if (!value.isObject())
        return false;

    out.name = readString(value.find("name"), "");
    out.type = scriptFieldTypeFromString(value.find("type"), ScriptFieldType::None);
    out.hidden = readBool(value.find("hidden"), false);

    const JsonValue* payload = value.find("value");
    switch (out.type) {
        case ScriptFieldType::Bool:
            out.boolValue = readBool(payload, false);
            break;
        case ScriptFieldType::Int:
            out.intValue = static_cast<int64_t>(payload ? payload->asNumber() : 0.0);
            break;
        case ScriptFieldType::Float:
            out.floatValue = static_cast<float>(payload ? payload->asNumber() : 0.0);
            break;
        case ScriptFieldType::String:
            out.stringValue = readString(payload, "");
            break;
        case ScriptFieldType::Vec3:
            if (payload)
                out.vec3Value = readVec3(*payload);
            break;
        case ScriptFieldType::Entity:
        case ScriptFieldType::Entity3D:
        case ScriptFieldType::EntityImage:
        case ScriptFieldType::EntityUI:
            out.entityValue = static_cast<uint64_t>(payload ? payload->asNumber() : 0.0);
            break;
        default:
            return false;
    }

    return !out.name.empty();
}

void writeScriptFieldValueBin(BinaryWriter& writer, const ScriptFieldValue& field)
{
    writer.writeString(field.name);
    const uint8_t type = static_cast<uint8_t>(field.type);
    writer.write(type);
    writer.write(field.hidden);
    switch (field.type) {
        case ScriptFieldType::Bool:
            writer.write(field.boolValue);
            break;
        case ScriptFieldType::Int:
            writer.write(field.intValue);
            break;
        case ScriptFieldType::Float:
            writer.write(field.floatValue);
            break;
        case ScriptFieldType::String:
            writer.writeString(field.stringValue);
            break;
        case ScriptFieldType::Vec3:
            writeVec3Bin(writer, field.vec3Value);
            break;
        case ScriptFieldType::Entity:
        case ScriptFieldType::Entity3D:
        case ScriptFieldType::EntityImage:
        case ScriptFieldType::EntityUI:
            writer.write(field.entityValue);
            break;
        default:
            break;
    }
}

bool readScriptFieldValueBin(BinaryReader& reader, ScriptFieldValue& field)
{
    if (!reader.readString(field.name))
        return false;
    uint8_t type = 0;
    if (!reader.read(type))
        return false;
    field.type = static_cast<ScriptFieldType>(type);
    if (!reader.read(field.hidden))
        return false;
    switch (field.type) {
        case ScriptFieldType::Bool:
            return reader.read(field.boolValue);
        case ScriptFieldType::Int:
            return reader.read(field.intValue);
        case ScriptFieldType::Float:
            return reader.read(field.floatValue);
        case ScriptFieldType::String:
            return reader.readString(field.stringValue);
        case ScriptFieldType::Vec3:
            readVec3Bin(reader, field.vec3Value);
            return true;
        case ScriptFieldType::Entity:
        case ScriptFieldType::Entity3D:
        case ScriptFieldType::EntityImage:
        case ScriptFieldType::EntityUI:
            return reader.read(field.entityValue);
        default:
            return false;
    }
}

} // namespace

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
SceneSerializer::SceneSerializer(std::shared_ptr<Scene> scene)
    : m_scene(std::move(scene)) {}

// ----------------------------------------------------------------------------
// JSON Serialization
// ----------------------------------------------------------------------------
std::string SceneSerializer::serializeToString()
{
    JsonWriter w(true, 2);

    w.beginObject();
    w.key("scene");
    w.value(m_scene->getName());

    w.key("entities");
    w.beginArray();

    const auto& entities = m_scene->getEntities();
    for (EntityID id : entities) {
        w.beginObject();
        w.key("id");
        w.value(static_cast<uint64_t>(id));

        if (auto* tc = m_scene->getComponent<TagComponent>(id)) {
            w.key("tag");
            w.value(tc->tag);
        }

        if (const EntityID parent = m_scene->getParent(id); parent != NULL_ENTITY) {
            w.key("parent");
            w.value(static_cast<uint64_t>(parent));
        }

        if (auto* tr = m_scene->getComponent<TransformComponent>(id)) {
            w.key("transform");
            w.beginObject();
            w.key("translation"); writeVec3(w, tr->translation);
            w.key("rotation");    writeVec3(w, tr->rotation);
            w.key("scale");       writeVec3(w, tr->scale);
            w.endObject();
        }

        if (auto* mr = m_scene->getComponent<MeshRendererComponent>(id)) {
            w.key("meshRenderer");
            w.beginObject();
            w.key("mesh");           w.value(mr->meshPath);
            w.key("material");       w.value(mr->materialPath);
            w.key("subMeshIndex");   w.value(static_cast<int64_t>(mr->subMeshIndex));
            w.key("preserveHierarchy"); w.value(mr->preserveHierarchy);
            w.key("castShadows");    w.value(mr->castShadows);
            w.key("receiveShadows"); w.value(mr->receiveShadows);
            w.key("visible");        w.value(mr->visible);
            w.endObject();
        }

        if (auto* animator = m_scene->getComponent<AnimatorComponent>(id)) {
            w.key("animator");
            w.beginObject();
            w.key("playing");       w.value(animator->playing);
            w.key("looping");       w.value(animator->looping);
            w.key("playbackSpeed"); w.value(animator->playbackSpeed);
            w.key("blendDuration"); w.value(animator->blendDuration);
            w.key("blendElapsed");  w.value(animator->blendElapsed);
            w.key("currentTime");   w.value(animator->currentTime);
            w.key("nextTime");      w.value(animator->nextTime);
            w.key("currentClip");   w.value(animator->currentClip);
            w.key("nextClip");      w.value(animator->nextClip);
            w.endObject();
        }

        if (auto* mc = m_scene->getComponent<MaterialComponent>(id)) {
            w.key("materialOverride");
            w.beginObject();
            w.key("path");      w.value(mc->materialPath);
            w.key("albedo");    writeVec4(w, mc->albedoColor);
            w.key("metallic");  w.value(mc->metallic);
            w.key("roughness"); w.value(mc->roughness);
            w.key("ao");        w.value(mc->ao);
            w.key("emissive");  writeVec3(w, mc->emissiveColor);
            w.key("emissiveStrength"); w.value(mc->emissiveStrength);
            w.key("doubleSided"); w.value(mc->doubleSided);
            w.key("alphaBlend");  w.value(mc->alphaBlend);
            w.key("alphaCutoff"); w.value(mc->alphaCutoff);
            w.key("albedoTex");   w.value(mc->albedoTexture);
            w.key("normalTex");   w.value(mc->normalTexture);
            w.key("metallicTex"); w.value(mc->metallicTexture);
            w.key("emissiveTex"); w.value(mc->emissiveTexture);
            w.endObject();
        }

        if (auto* cc = m_scene->getComponent<CameraComponent>(id)) {
            w.key("camera");
            w.beginObject();
            w.key("primary");     w.value(cc->primary);
            w.key("fixedAspect"); w.value(cc->fixedAspect);
            w.key("fovY");        w.value(cc->camera.getFovY());
            w.key("near");        w.value(cc->camera.getNearClip());
            w.key("far");         w.value(cc->camera.getFarClip());
            w.endObject();
        }

        if (auto* sb = m_scene->getComponent<SkyboxComponent>(id)) {
            w.key("skybox");
            w.beginObject();
            w.key("enabled");   w.value(sb->enabled);
            w.key("texture");   w.value(sb->texturePath);
            w.key("intensity"); w.value(sb->intensity);
            w.endObject();
        }

        if (auto* fg = m_scene->getComponent<FogComponent>(id)) {
            w.key("fog");
            w.beginObject();
            w.key("enabled");       w.value(fg->enabled);
            w.key("color");         writeVec3(w, fg->color);
            w.key("density");       w.value(fg->density);
            w.key("height");        w.value(fg->height);
            w.key("heightFalloff"); w.value(fg->heightFalloff);
            w.key("start");         w.value(fg->start);
            w.endObject();
        }

        if (auto* vf = m_scene->getComponent<VolumetricFogComponent>(id)) {
            w.key("volumetricFog");
            w.beginObject();
            w.key("enabled");       w.value(vf->enabled);
            w.key("color");         writeVec3(w, vf->color);
            w.key("density");       w.value(vf->density);
            w.key("intensity");     w.value(vf->intensity);
            w.key("anisotropy");    w.value(vf->anisotropy);
            w.key("height");        w.value(vf->height);
            w.key("heightFalloff"); w.value(vf->heightFalloff);
            w.key("startDistance"); w.value(vf->startDistance);
            w.key("maxOpacity");    w.value(vf->maxOpacity);
            w.endObject();
        }

        if (auto* vf = m_scene->getComponent<LocalVolumetricFogComponent>(id)) {
            w.key("localVolumetricFog");
            w.beginObject();
            w.key("enabled");      w.value(vf->enabled);
            w.key("color");        writeVec3(w, vf->color);
            w.key("density");      w.value(vf->density);
            w.key("intensity");    w.value(vf->intensity);
            w.key("extents");      writeVec3(w, vf->extents);
            w.key("edgeSoftness"); w.value(vf->edgeSoftness);
            w.endObject();
        }

        if (auto* clouds = m_scene->getComponent<VolumetricCloudComponent>(id)) {
            w.key("volumetricClouds");
            w.beginObject();
            w.key("enabled");    w.value(clouds->enabled);
            w.key("preset");     w.value(static_cast<int64_t>(clouds->preset));
            w.key("coverage");   w.value(clouds->coverage);
            w.key("density");    w.value(clouds->density);
            w.key("altitude");   w.value(clouds->altitude);
            w.key("thickness");  w.value(clouds->thickness);
            w.key("scale");      w.value(clouds->scale);
            w.key("speed");      w.value(clouds->speed);
            w.key("darkness");   w.value(clouds->darkness);
            w.key("tint");       writeVec3(w, clouds->tint);
            w.endObject();
        }

        if (auto* flare = m_scene->getComponent<LensFlareComponent>(id)) {
            w.key("lensFlare");
            w.beginObject();
            w.key("enabled");       w.value(flare->enabled);
            w.key("intensity");     w.value(flare->intensity);
            w.key("threshold");     w.value(flare->threshold);
            w.key("haloWidth");     w.value(flare->haloWidth);
            w.key("ghostSpacing");  w.value(flare->ghostSpacing);
            w.key("dirtIntensity"); w.value(flare->dirtIntensity);
            w.key("tint");          writeVec3(w, flare->tint);
            w.endObject();
        }

        if (auto* probe = m_scene->getComponent<ReflectionProbeComponent>(id)) {
            w.key("reflectionProbe");
            w.beginObject();
            w.key("enabled");  w.value(probe->enabled);
            w.key("asset");    w.value(probe->assetPath);
            w.key("priority"); w.value(static_cast<int64_t>(probe->priority));
            w.endObject();
        }

        if (auto* volume = m_scene->getComponent<IrradianceProbeVolumeComponent>(id)) {
            w.key("irradianceProbeVolume");
            w.beginObject();
            w.key("enabled");       w.value(volume->enabled);
            w.key("extents");       writeVec3(w, volume->extents);
            w.key("probeCounts");   writeVec3(w, volume->probeCounts);
            w.key("tint");          writeVec3(w, volume->tint);
            w.key("intensity");     w.value(volume->intensity);
            w.key("skyWeight");     w.value(volume->skyWeight);
            w.key("bounceWeight");  w.value(volume->bounceWeight);
            w.key("normalBias");    w.value(volume->normalBias);
            w.key("leakReduction"); w.value(volume->leakReduction);
            w.key("dynamicUpdate"); w.value(volume->dynamicUpdate);
            w.endObject();
        }

        if (auto* lc = m_scene->getComponent<LightComponent>(id)) {
            w.key("light");
            w.beginObject();
            w.key("type");        w.value(static_cast<int64_t>(lc->type));
            w.key("color");       writeVec3(w, lc->color);
            w.key("intensity");   w.value(lc->intensity);
            w.key("range");       w.value(lc->range);
            w.key("innerAngle");  w.value(lc->innerAngle);
            w.key("outerAngle");  w.value(lc->outerAngle);
            w.key("castShadows"); w.value(lc->castShadows);
            w.key("cookieTexture"); w.value(lc->cookieTexture);
            w.key("cookieStrength"); w.value(lc->cookieStrength);
            w.endObject();
        }

        if (auto* sc = m_scene->getComponent<ScriptComponent>(id)) {
            w.key("script");
            w.beginObject();
            w.key("className");
            w.value(sc->className);
            w.key("fields");
            w.beginArray();
            for (const ScriptFieldValue& field : sc->fieldValues)
                writeScriptFieldValue(w, field);
            w.endArray();
            w.endObject();
        }

        if (auto* rb = m_scene->getComponent<RigidBodyComponent>(id)) {
            w.key("rigidBody");
            w.beginObject();
            w.key("type");           w.value(static_cast<int64_t>(rb->type));
            w.key("mass");           w.value(rb->mass);
            w.key("linearDamping");  w.value(rb->linearDamping);
            w.key("angularDamping"); w.value(rb->angularDamping);
            w.key("useGravity");     w.value(rb->useGravity);
            w.key("gravityScale");   w.value(rb->gravityScale);
            w.key("isKinematic");    w.value(rb->isKinematic);
            w.key("simulatePhysics"); w.value(rb->simulatePhysics);
            w.key("lockRotation");   w.value(rb->lockRotation);
            w.key("lockPositionX");  w.value(rb->lockPositionX);
            w.key("lockPositionY");  w.value(rb->lockPositionY);
            w.key("lockPositionZ");  w.value(rb->lockPositionZ);
            w.key("lockRotationX");  w.value(rb->lockRotationX);
            w.key("lockRotationY");  w.value(rb->lockRotationY);
            w.key("lockRotationZ");  w.value(rb->lockRotationZ);
            w.key("continuousCollision"); w.value(rb->continuousCollision);
            w.key("allowSleeping");  w.value(rb->allowSleeping);
            w.key("collisionLayer"); w.value(static_cast<int64_t>(rb->collisionLayer));
            w.key("linearVelocity"); writeVec3(w, rb->linearVelocity);
            w.key("angularVelocity"); writeVec3(w, rb->angularVelocity);
            w.endObject();
        }

        if (auto* bc = m_scene->getComponent<BoxColliderComponent>(id)) {
            w.key("boxCollider");
            w.beginObject();
            w.key("halfExtents"); writeVec3(w, bc->halfExtents);
            w.key("offset");      writeVec3(w, bc->offset);
            w.key("friction");    w.value(bc->friction);
            w.key("restitution"); w.value(bc->restitution);
            w.key("isTrigger");   w.value(bc->isTrigger);
            w.endObject();
        }

        if (auto* ui = m_scene->getComponent<UIElementComponent>(id)) {
            w.key("uiElement");
            w.beginObject();
            w.key("kind");        w.value(static_cast<int64_t>(ui->kind));
            w.key("shape");       w.value(static_cast<int64_t>(ui->shape));
            w.key("visible");     w.value(ui->visible);
            w.key("screenSpace"); w.value(ui->screenSpace);
            w.key("billboard");   w.value(ui->billboard);
            w.key("text");        w.value(ui->text);
            w.key("imagePath");   w.value(ui->imagePath);
            w.key("color");       writeVec4(w, ui->color);
            w.key("size");        writeVec2(w, ui->size);
            w.key("fontSize");    w.value(ui->fontSize);
            w.key("depth");       w.value(ui->depth);
            w.endObject();
        }

        if (auto* terrain = m_scene->getComponent<TerrainComponent>(id)) {
            w.key("terrain");
            w.beginObject();
            w.key("resolution");       w.value(static_cast<int64_t>(terrain->resolution));
            w.key("size");             writeVec2(w, {terrain->sizeX, terrain->sizeZ});
            w.key("maxHeight");        w.value(terrain->maxHeight);
            w.key("uvScale");          w.value(terrain->uvScale);
            w.key("lowColor");         writeVec4(w, terrain->lowColor);
            w.key("midColor");         writeVec4(w, terrain->midColor);
            w.key("highColor");        writeVec4(w, terrain->highColor);
            w.key("castShadows");      w.value(terrain->castShadows);
            w.key("receiveShadows");   w.value(terrain->receiveShadows);
            w.key("collisionEnabled"); w.value(terrain->collisionEnabled);
            w.key("heights");
            w.beginArray();
            for (float h : terrain->heights)
                w.value(h);
            w.endArray();
            w.endObject();
        }

        if (auto* sculpt = m_scene->getComponent<TerrainSculptComponent>(id)) {
            w.key("terrainSculpt");
            w.beginObject();
            w.key("tool");          w.value(static_cast<int64_t>(sculpt->tool));
            w.key("brushRadius");   w.value(sculpt->brushRadius);
            w.key("brushStrength"); w.value(sculpt->brushStrength);
            w.key("brushFalloff");  w.value(sculpt->brushFalloff);
            w.key("flattenTarget"); w.value(sculpt->flattenTarget);
            w.key("noiseScale");    w.value(sculpt->noiseScale);
            w.key("terraceSpacing"); w.value(sculpt->terraceSpacing);
            w.key("erosionAmount"); w.value(sculpt->erosionAmount);
            w.key("sharpenAmount"); w.value(sculpt->sharpenAmount);
            w.key("brushCenter");   writeVec2(w, sculpt->brushCenter);
            w.key("autoRebuild");   w.value(sculpt->autoRebuild);
            w.endObject();
        }

        if (auto* foliage = m_scene->getComponent<TerrainFoliageComponent>(id)) {
            w.key("terrainFoliage");
            w.beginObject();
            w.key("treesEnabled");    w.value(foliage->treesEnabled);
            w.key("grassEnabled");    w.value(foliage->grassEnabled);
            w.key("treeCount");       w.value(static_cast<int64_t>(foliage->treeCount));
            w.key("grassCount");      w.value(static_cast<int64_t>(foliage->grassCount));
            w.key("treeMeshPath");    w.value(foliage->treeMeshPath);
            w.key("grassMeshPath");   w.value(foliage->grassMeshPath);
            w.key("treeMinScale");    w.value(foliage->treeMinScale);
            w.key("treeMaxScale");    w.value(foliage->treeMaxScale);
            w.key("grassMinScale");   w.value(foliage->grassMinScale);
            w.key("grassMaxScale");   w.value(foliage->grassMaxScale);
            w.key("placementJitter"); w.value(foliage->placementJitter);
            w.key("brushRadius");     w.value(foliage->brushRadius);
            w.key("brushDensity");    w.value(foliage->brushDensity);
            w.key("brushCenter");     writeVec2(w, foliage->brushCenter);
            w.key("minHeight");       w.value(foliage->minHeight);
            w.key("maxHeight");       w.value(foliage->maxHeight);
            w.key("maxSlopeDegrees"); w.value(foliage->maxSlopeDegrees);
            w.key("randomSeed");      w.value(static_cast<int64_t>(foliage->randomSeed));
            w.key("treeTrunkColor");  writeVec4(w, foliage->treeTrunkColor);
            w.key("treeLeafColor");   writeVec4(w, foliage->treeLeafColor);
            w.key("grassColor");      writeVec4(w, foliage->grassColor);
            w.key("paintedTrees");     writeVec4Array(w, foliage->paintedTrees);
            w.key("paintedGrass");     writeVec4Array(w, foliage->paintedGrass);
            w.endObject();
        }

        if (auto* water = m_scene->getComponent<WaterBodyComponent>(id)) {
            w.key("waterBody");
            w.beginObject();
            w.key("type");               w.value(static_cast<int64_t>(water->type));
            w.key("resolution");         w.value(static_cast<int64_t>(water->resolution));
            w.key("size");               writeVec2(w, water->size);
            w.key("depth");              w.value(water->depth);
            w.key("surfaceColor");       writeVec4(w, water->surfaceColor);
            w.key("bottomColor");        writeVec4(w, water->bottomColor);
            w.key("transparency");       w.value(water->transparency);
            w.key("waveAmplitude");      w.value(water->waveAmplitude);
            w.key("waveLength");         w.value(water->waveLength);
            w.key("waveSpeed");          w.value(water->waveSpeed);
            w.key("choppiness");         w.value(water->choppiness);
            w.key("roughness");          w.value(water->roughness);
            w.key("foamIntensity");      w.value(water->foamIntensity);
            w.key("edgeFade");           w.value(water->edgeFade);
            w.key("flowDirection");      writeVec2(w, water->flowDirection);
            w.key("flowSpeed");          w.value(water->flowSpeed);
            w.key("fluidDensity");       w.value(water->fluidDensity);
            w.key("drag");               w.value(water->drag);
            w.key("buoyancyMultiplier"); w.value(water->buoyancyMultiplier);
            w.key("affectsRigidBodies"); w.value(water->affectsRigidBodies);
            w.endObject();
        }

        w.endObject();
    }

    w.endArray();
    w.endObject();
    return w.str();
}

bool SceneSerializer::serialize(const std::string& filepath)
{
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        DEMON_LOG_ERROR("SceneSerializer: cannot write '{}'", filepath);
        return false;
    }
    file << serializeToString();
    DEMON_LOG_INFO("Scene saved: '{}'", filepath);
    return true;
}

bool SceneSerializer::deserializeFromString(const std::string& json)
{
    JsonDocument doc;
    if (!doc.parse(json)) {
        DEMON_LOG_ERROR("SceneSerializer: JSON parse error at {}: {}",
            doc.errorOffset(), doc.error());
        return false;
    }

    const JsonValue& root = doc.root();
    if (!root.isObject()) {
        DEMON_LOG_ERROR("SceneSerializer: invalid JSON root");
        return false;
    }

    if (auto* sceneName = root.find("scene"); sceneName && sceneName->isString())
        m_scene->setName(sceneName->asString());

    const JsonValue* entities = root.find("entities");
    if (!entities || !entities->isArray()) {
        DEMON_LOG_WARN("SceneSerializer: no entities array found");
        return false;
    }

    std::vector<std::pair<EntityID, EntityID>> pendingParents;

    for (const JsonValue& ent : entities->asArray()) {
        if (!ent.isObject()) continue;

        const JsonValue* idVal = ent.find("id");
        uint64_t id = idVal ? static_cast<uint64_t>(idVal->asNumber()) : 0;
        if (id == 0) continue;

        std::string tag = readString(ent.find("tag"), "Entity");
        Entity e = m_scene->createEntityWithID(UUID(id), tag);
        if (const JsonValue* parentVal = ent.find("parent"); parentVal && parentVal->isNumber()) {
            const EntityID parent = static_cast<EntityID>(parentVal->asNumber());
            if (parent != NULL_ENTITY)
                pendingParents.emplace_back(id, parent);
        }

        if (const JsonValue* tr = ent.find("transform"); tr && tr->isObject()) {
            TransformComponent comp;
            if (auto* v = tr->find("translation")) comp.translation = readVec3(*v);
            if (auto* v = tr->find("rotation"))    comp.rotation    = readVec3(*v);
            if (auto* v = tr->find("scale"))       comp.scale       = readVec3(*v);
            e.addComponent<TransformComponent>() = comp;
        }

        if (const JsonValue* mr = ent.find("meshRenderer"); mr && mr->isObject()) {
            MeshRendererComponent comp;
            comp.meshPath        = readString(mr->find("mesh"), "");
            comp.materialPath    = readString(mr->find("material"), "");
            comp.subMeshIndex    = readInt(mr->find("subMeshIndex"), comp.subMeshIndex);
            comp.preserveHierarchy = readBool(mr->find("preserveHierarchy"), comp.preserveHierarchy);
            comp.castShadows     = readBool(mr->find("castShadows"), true);
            comp.receiveShadows  = readBool(mr->find("receiveShadows"), true);
            comp.visible         = readBool(mr->find("visible"), true);
            comp.meshHandle      = 0;
            comp.materialHandle  = 0;
            e.addComponent<MeshRendererComponent>() = comp;
        }

        if (const JsonValue* animator = ent.find("animator"); animator && animator->isObject()) {
            AnimatorComponent comp;
            comp.playing       = readBool(animator->find("playing"), comp.playing);
            comp.looping       = readBool(animator->find("looping"), comp.looping);
            comp.playbackSpeed = readFloat(animator->find("playbackSpeed"), comp.playbackSpeed);
            comp.blendDuration = readFloat(animator->find("blendDuration"), comp.blendDuration);
            comp.blendElapsed  = readFloat(animator->find("blendElapsed"), comp.blendElapsed);
            comp.currentTime   = readFloat(animator->find("currentTime"), comp.currentTime);
            comp.nextTime      = readFloat(animator->find("nextTime"), comp.nextTime);
            comp.currentClip   = readString(animator->find("currentClip"), "");
            comp.nextClip      = readString(animator->find("nextClip"), "");
            e.addComponent<AnimatorComponent>() = std::move(comp);
        }

        if (const JsonValue* mc = ent.find("materialOverride"); mc && mc->isObject()) {
            MaterialComponent comp;
            comp.materialPath      = readString(mc->find("path"), "");
            if (auto* v = mc->find("albedo")) comp.albedoColor = readVec4(*v);
            comp.metallic          = readFloat(mc->find("metallic"), comp.metallic);
            comp.roughness         = readFloat(mc->find("roughness"), comp.roughness);
            comp.ao                = readFloat(mc->find("ao"), comp.ao);
            if (auto* v = mc->find("emissive")) comp.emissiveColor = readVec3(*v);
            comp.emissiveStrength  = readFloat(mc->find("emissiveStrength"), comp.emissiveStrength);
            comp.doubleSided       = readBool(mc->find("doubleSided"), comp.doubleSided);
            comp.alphaBlend        = readBool(mc->find("alphaBlend"), comp.alphaBlend);
            comp.alphaCutoff       = readFloat(mc->find("alphaCutoff"), comp.alphaCutoff);
            comp.albedoTexture     = readString(mc->find("albedoTex"), "");
            comp.normalTexture     = readString(mc->find("normalTex"), "");
            comp.metallicTexture   = readString(mc->find("metallicTex"), "");
            comp.emissiveTexture   = readString(mc->find("emissiveTex"), "");
            comp.dirty             = true;
            e.addComponent<MaterialComponent>() = comp;
        }

        if (const JsonValue* cc = ent.find("camera"); cc && cc->isObject()) {
            CameraComponent comp;
            comp.primary     = readBool(cc->find("primary"), comp.primary);
            comp.fixedAspect = readBool(cc->find("fixedAspect"), comp.fixedAspect);
            float fovY       = readFloat(cc->find("fovY"), comp.camera.getFovY());
            float nearC      = readFloat(cc->find("near"), comp.camera.getNearClip());
            float farC       = readFloat(cc->find("far"), comp.camera.getFarClip());
            comp.camera.setPerspective(fovY, 16.0f / 9.0f, nearC, farC);
            e.addComponent<CameraComponent>() = comp;
        }

        if (const JsonValue* sb = ent.find("skybox"); sb && sb->isObject()) {
            SkyboxComponent comp;
            comp.enabled    = readBool(sb->find("enabled"), comp.enabled);
            comp.texturePath = readString(sb->find("texture"), "");
            comp.intensity  = readFloat(sb->find("intensity"), comp.intensity);
            e.addComponent<SkyboxComponent>() = comp;
        }

        if (const JsonValue* fg = ent.find("fog"); fg && fg->isObject()) {
            FogComponent comp;
            comp.enabled       = readBool(fg->find("enabled"), comp.enabled);
            if (auto* v = fg->find("color")) comp.color = readVec3(*v);
            comp.density       = readFloat(fg->find("density"), comp.density);
            comp.height        = readFloat(fg->find("height"), comp.height);
            comp.heightFalloff = readFloat(fg->find("heightFalloff"), comp.heightFalloff);
            comp.start         = readFloat(fg->find("start"), comp.start);
            e.addComponent<FogComponent>() = comp;
        }

        if (const JsonValue* vf = ent.find("volumetricFog"); vf && vf->isObject()) {
            VolumetricFogComponent comp;
            comp.enabled       = readBool(vf->find("enabled"), comp.enabled);
            if (auto* v = vf->find("color")) comp.color = readVec3(*v);
            comp.density       = readFloat(vf->find("density"), comp.density);
            comp.intensity     = readFloat(vf->find("intensity"), comp.intensity);
            comp.anisotropy    = readFloat(vf->find("anisotropy"), comp.anisotropy);
            comp.height        = readFloat(vf->find("height"), comp.height);
            comp.heightFalloff = readFloat(vf->find("heightFalloff"), comp.heightFalloff);
            comp.startDistance = readFloat(vf->find("startDistance"), comp.startDistance);
            comp.maxOpacity    = readFloat(vf->find("maxOpacity"), comp.maxOpacity);
            e.addComponent<VolumetricFogComponent>() = comp;
        }

        if (const JsonValue* vf = ent.find("localVolumetricFog"); vf && vf->isObject()) {
            LocalVolumetricFogComponent comp;
            comp.enabled = readBool(vf->find("enabled"), comp.enabled);
            if (auto* v = vf->find("color")) comp.color = readVec3(*v);
            comp.density = readFloat(vf->find("density"), comp.density);
            comp.intensity = readFloat(vf->find("intensity"), comp.intensity);
            if (auto* v = vf->find("extents")) comp.extents = readVec3(*v);
            comp.edgeSoftness = readFloat(vf->find("edgeSoftness"), comp.edgeSoftness);
            e.addComponent<LocalVolumetricFogComponent>() = comp;
        }

        if (const JsonValue* clouds = ent.find("volumetricClouds"); clouds && clouds->isObject()) {
            VolumetricCloudComponent comp;
            comp.enabled      = readBool(clouds->find("enabled"), comp.enabled);
            comp.preset       = static_cast<VolumetricCloudPreset>(readInt(clouds->find("preset"), static_cast<int>(comp.preset)));
            comp.coverage     = readFloat(clouds->find("coverage"), comp.coverage);
            comp.density      = readFloat(clouds->find("density"), comp.density);
            comp.altitude     = readFloat(clouds->find("altitude"), comp.altitude);
            comp.thickness    = readFloat(clouds->find("thickness"), comp.thickness);
            comp.scale        = readFloat(clouds->find("scale"), comp.scale);
            comp.speed        = readFloat(clouds->find("speed"), comp.speed);
            comp.darkness     = readFloat(clouds->find("darkness"), comp.darkness);
            if (auto* v = clouds->find("tint")) comp.tint = readVec3(*v);
            e.addComponent<VolumetricCloudComponent>() = comp;
        }

        if (const JsonValue* flare = ent.find("lensFlare"); flare && flare->isObject()) {
            LensFlareComponent comp;
            comp.enabled       = readBool(flare->find("enabled"), comp.enabled);
            comp.intensity     = readFloat(flare->find("intensity"), comp.intensity);
            comp.threshold     = readFloat(flare->find("threshold"), comp.threshold);
            comp.haloWidth     = readFloat(flare->find("haloWidth"), comp.haloWidth);
            comp.ghostSpacing  = readFloat(flare->find("ghostSpacing"), comp.ghostSpacing);
            comp.dirtIntensity = readFloat(flare->find("dirtIntensity"), comp.dirtIntensity);
            if (auto* v = flare->find("tint")) comp.tint = readVec3(*v);
            e.addComponent<LensFlareComponent>() = comp;
        }

        if (const JsonValue* probe = ent.find("reflectionProbe"); probe && probe->isObject()) {
            ReflectionProbeComponent comp;
            comp.enabled  = readBool(probe->find("enabled"), comp.enabled);
            comp.assetPath = readString(probe->find("asset"), "");
            comp.priority = readInt(probe->find("priority"), comp.priority);
            e.addComponent<ReflectionProbeComponent>() = comp;
        }

        if (const JsonValue* volume = ent.find("irradianceProbeVolume"); volume && volume->isObject()) {
            IrradianceProbeVolumeComponent comp;
            comp.enabled = readBool(volume->find("enabled"), comp.enabled);
            if (auto* v = volume->find("extents")) comp.extents = readVec3(*v);
            if (auto* v = volume->find("probeCounts")) comp.probeCounts = readVec3(*v);
            if (auto* v = volume->find("tint")) comp.tint = readVec3(*v);
            comp.intensity = readFloat(volume->find("intensity"), comp.intensity);
            comp.skyWeight = readFloat(volume->find("skyWeight"), comp.skyWeight);
            comp.bounceWeight = readFloat(volume->find("bounceWeight"), comp.bounceWeight);
            comp.normalBias = readFloat(volume->find("normalBias"), comp.normalBias);
            comp.leakReduction = readFloat(volume->find("leakReduction"), comp.leakReduction);
            comp.dynamicUpdate = readBool(volume->find("dynamicUpdate"), comp.dynamicUpdate);
            e.addComponent<IrradianceProbeVolumeComponent>() = comp;
        }

        if (const JsonValue* lc = ent.find("light"); lc && lc->isObject()) {
            LightComponent comp;
            comp.type        = static_cast<LightType>(readInt(lc->find("type"), static_cast<int>(comp.type)));
            if (auto* v = lc->find("color")) comp.color = readVec3(*v);
            comp.intensity   = readFloat(lc->find("intensity"), comp.intensity);
            comp.range       = readFloat(lc->find("range"), comp.range);
            comp.innerAngle  = readFloat(lc->find("innerAngle"), comp.innerAngle);
            comp.outerAngle  = readFloat(lc->find("outerAngle"), comp.outerAngle);
            comp.castShadows = readBool(lc->find("castShadows"), comp.castShadows);
            comp.cookieTexture = readString(lc->find("cookieTexture"), comp.cookieTexture);
            comp.cookieStrength = readFloat(lc->find("cookieStrength"), comp.cookieStrength);
            e.addComponent<LightComponent>() = comp;
        }

        if (const JsonValue* sc = ent.find("script")) {
            ScriptComponent comp;
            if (sc->isString()) {
                comp.className = sc->asString();
                e.addComponent<ScriptComponent>() = comp;
            } else if (sc->isObject()) {
                comp.className = readString(sc->find("className"), "");
                if (const JsonValue* fields = sc->find("fields"); fields && fields->isArray()) {
                    for (const JsonValue& field : fields->asArray()) {
                        ScriptFieldValue parsedField;
                        if (readScriptFieldValue(field, parsedField))
                            comp.fieldValues.push_back(std::move(parsedField));
                    }
                }
                e.addComponent<ScriptComponent>() = comp;
            }
        }

        if (const JsonValue* rb = ent.find("rigidBody"); rb && rb->isObject()) {
            RigidBodyComponent comp;
            comp.type           = static_cast<BodyType>(readInt(rb->find("type"), static_cast<int>(comp.type)));
            comp.mass           = readFloat(rb->find("mass"), comp.mass);
            comp.linearDamping  = readFloat(rb->find("linearDamping"), comp.linearDamping);
            comp.angularDamping = readFloat(rb->find("angularDamping"), comp.angularDamping);
            comp.useGravity     = readBool(rb->find("useGravity"), comp.useGravity);
            comp.gravityScale   = readFloat(rb->find("gravityScale"), comp.gravityScale);
            comp.isKinematic    = readBool(rb->find("isKinematic"), comp.isKinematic);
            comp.simulatePhysics = readBool(rb->find("simulatePhysics"), comp.simulatePhysics);
            comp.lockRotation   = readBool(rb->find("lockRotation"), comp.lockRotation);
            comp.lockPositionX  = readBool(rb->find("lockPositionX"), comp.lockPositionX);
            comp.lockPositionY  = readBool(rb->find("lockPositionY"), comp.lockPositionY);
            comp.lockPositionZ  = readBool(rb->find("lockPositionZ"), comp.lockPositionZ);
            comp.lockRotationX  = readBool(rb->find("lockRotationX"), comp.lockRotationX);
            comp.lockRotationY  = readBool(rb->find("lockRotationY"), comp.lockRotationY);
            comp.lockRotationZ  = readBool(rb->find("lockRotationZ"), comp.lockRotationZ);
            comp.continuousCollision = readBool(rb->find("continuousCollision"), comp.continuousCollision);
            comp.allowSleeping  = readBool(rb->find("allowSleeping"), comp.allowSleeping);
            comp.collisionLayer = static_cast<uint32_t>(std::max(0, readInt(rb->find("collisionLayer"), static_cast<int>(comp.collisionLayer))));
            if (auto* v = rb->find("linearVelocity"))  comp.linearVelocity = readVec3(*v);
            if (auto* v = rb->find("angularVelocity")) comp.angularVelocity = readVec3(*v);
            e.addComponent<RigidBodyComponent>() = comp;
        }

        if (const JsonValue* bc = ent.find("boxCollider"); bc && bc->isObject()) {
            BoxColliderComponent comp;
            if (auto* v = bc->find("halfExtents")) comp.halfExtents = readVec3(*v);
            if (auto* v = bc->find("offset"))      comp.offset      = readVec3(*v);
            comp.friction    = readFloat(bc->find("friction"), comp.friction);
            comp.restitution = readFloat(bc->find("restitution"), comp.restitution);
            comp.isTrigger   = readBool(bc->find("isTrigger"), comp.isTrigger);
            e.addComponent<BoxColliderComponent>() = comp;
        }

        if (const JsonValue* ui = ent.find("uiElement"); ui && ui->isObject()) {
            UIElementComponent comp;
            comp.kind = static_cast<UIElementKind>(readInt(ui->find("kind"), static_cast<int>(comp.kind)));
            comp.shape = static_cast<UIShapeKind>(readInt(ui->find("shape"), static_cast<int>(comp.shape)));
            comp.visible = readBool(ui->find("visible"), comp.visible);
            comp.screenSpace = readBool(ui->find("screenSpace"), comp.screenSpace);
            comp.billboard = readBool(ui->find("billboard"), comp.billboard);
            comp.text = readString(ui->find("text"), comp.text);
            comp.imagePath = readString(ui->find("imagePath"), comp.imagePath);
            if (auto* v = ui->find("color")) comp.color = readVec4(*v);
            if (auto* v = ui->find("size")) comp.size = readVec2(*v);
            comp.fontSize = readFloat(ui->find("fontSize"), comp.fontSize);
            comp.depth = readFloat(ui->find("depth"), comp.depth);
            e.addComponent<UIElementComponent>() = comp;
        }

        if (const JsonValue* terrain = ent.find("terrain"); terrain && terrain->isObject()) {
            TerrainComponent comp;
            comp.resolution = static_cast<uint32_t>(readInt(terrain->find("resolution"), static_cast<int>(comp.resolution)));
            if (auto* v = terrain->find("size")) {
                const glm::vec2 size = readVec2(*v);
                if (size.x > 0.0f) comp.sizeX = size.x;
                if (size.y > 0.0f) comp.sizeZ = size.y;
            }
            comp.maxHeight        = readFloat(terrain->find("maxHeight"), comp.maxHeight);
            comp.uvScale          = readFloat(terrain->find("uvScale"), comp.uvScale);
            if (auto* v = terrain->find("lowColor"))  comp.lowColor = readVec4(*v);
            if (auto* v = terrain->find("midColor"))  comp.midColor = readVec4(*v);
            if (auto* v = terrain->find("highColor")) comp.highColor = readVec4(*v);
            comp.castShadows      = readBool(terrain->find("castShadows"), comp.castShadows);
            comp.receiveShadows   = readBool(terrain->find("receiveShadows"), comp.receiveShadows);
            comp.collisionEnabled = readBool(terrain->find("collisionEnabled"), comp.collisionEnabled);
            if (const JsonValue* heights = terrain->find("heights"); heights && heights->isArray()) {
                comp.heights.reserve(heights->asArray().size());
                for (const JsonValue& value : heights->asArray())
                    comp.heights.push_back(static_cast<float>(value.asNumber()));
            }
            comp.dirty = true;
            e.addComponent<TerrainComponent>() = comp;
        }

        if (const JsonValue* sculpt = ent.find("terrainSculpt"); sculpt && sculpt->isObject()) {
            TerrainSculptComponent comp;
            comp.tool          = static_cast<TerrainSculptTool>(readInt(sculpt->find("tool"), static_cast<int>(comp.tool)));
            comp.brushRadius   = readFloat(sculpt->find("brushRadius"), comp.brushRadius);
            comp.brushStrength = readFloat(sculpt->find("brushStrength"), comp.brushStrength);
            comp.brushFalloff  = readFloat(sculpt->find("brushFalloff"), comp.brushFalloff);
            comp.flattenTarget = readFloat(sculpt->find("flattenTarget"), comp.flattenTarget);
            comp.noiseScale    = readFloat(sculpt->find("noiseScale"), comp.noiseScale);
            comp.terraceSpacing = readFloat(sculpt->find("terraceSpacing"), comp.terraceSpacing);
            comp.erosionAmount = readFloat(sculpt->find("erosionAmount"), comp.erosionAmount);
            comp.sharpenAmount = readFloat(sculpt->find("sharpenAmount"), comp.sharpenAmount);
            if (auto* v = sculpt->find("brushCenter")) comp.brushCenter = readVec2(*v);
            comp.autoRebuild   = readBool(sculpt->find("autoRebuild"), comp.autoRebuild);
            e.addComponent<TerrainSculptComponent>() = comp;
        }

        if (const JsonValue* foliage = ent.find("terrainFoliage"); foliage && foliage->isObject()) {
            TerrainFoliageComponent comp;
            comp.treesEnabled    = readBool(foliage->find("treesEnabled"), comp.treesEnabled);
            comp.grassEnabled    = readBool(foliage->find("grassEnabled"), comp.grassEnabled);
            comp.treeCount       = static_cast<uint32_t>(readInt(foliage->find("treeCount"), static_cast<int>(comp.treeCount)));
            comp.grassCount      = static_cast<uint32_t>(readInt(foliage->find("grassCount"), static_cast<int>(comp.grassCount)));
            comp.treeMeshPath    = readString(foliage->find("treeMeshPath"), comp.treeMeshPath);
            comp.grassMeshPath   = readString(foliage->find("grassMeshPath"), comp.grassMeshPath);
            comp.treeMinScale    = readFloat(foliage->find("treeMinScale"), comp.treeMinScale);
            comp.treeMaxScale    = readFloat(foliage->find("treeMaxScale"), comp.treeMaxScale);
            comp.grassMinScale   = readFloat(foliage->find("grassMinScale"), comp.grassMinScale);
            comp.grassMaxScale   = readFloat(foliage->find("grassMaxScale"), comp.grassMaxScale);
            comp.placementJitter = readFloat(foliage->find("placementJitter"), comp.placementJitter);
            comp.brushRadius     = readFloat(foliage->find("brushRadius"), comp.brushRadius);
            comp.brushDensity    = readFloat(foliage->find("brushDensity"), comp.brushDensity);
            if (auto* v = foliage->find("brushCenter")) comp.brushCenter = readVec2(*v);
            comp.minHeight       = readFloat(foliage->find("minHeight"), comp.minHeight);
            comp.maxHeight       = readFloat(foliage->find("maxHeight"), comp.maxHeight);
            comp.maxSlopeDegrees = readFloat(foliage->find("maxSlopeDegrees"), comp.maxSlopeDegrees);
            comp.randomSeed      = static_cast<uint32_t>(readInt(foliage->find("randomSeed"), static_cast<int>(comp.randomSeed)));
            if (auto* v = foliage->find("treeTrunkColor")) comp.treeTrunkColor = readVec4(*v);
            if (auto* v = foliage->find("treeLeafColor"))  comp.treeLeafColor = readVec4(*v);
            if (auto* v = foliage->find("grassColor"))     comp.grassColor = readVec4(*v);
            comp.paintedTrees = readVec4Array(foliage->find("paintedTrees"));
            comp.paintedGrass = readVec4Array(foliage->find("paintedGrass"));
            comp.dirty = true;
            e.addComponent<TerrainFoliageComponent>() = comp;
        }

        if (const JsonValue* water = ent.find("waterBody"); water && water->isObject()) {
            WaterBodyComponent comp;
            comp.type               = static_cast<WaterBodyType>(readInt(water->find("type"), static_cast<int>(comp.type)));
            comp.resolution         = static_cast<uint32_t>(readInt(water->find("resolution"), static_cast<int>(comp.resolution)));
            if (auto* v = water->find("size")) comp.size = readVec2(*v);
            comp.depth              = readFloat(water->find("depth"), comp.depth);
            if (auto* v = water->find("surfaceColor")) comp.surfaceColor = readVec4(*v);
            if (auto* v = water->find("bottomColor"))  comp.bottomColor = readVec4(*v);
            comp.transparency       = readFloat(water->find("transparency"), comp.transparency);
            comp.waveAmplitude      = readFloat(water->find("waveAmplitude"), comp.waveAmplitude);
            comp.waveLength         = readFloat(water->find("waveLength"), comp.waveLength);
            comp.waveSpeed          = readFloat(water->find("waveSpeed"), comp.waveSpeed);
            comp.choppiness         = readFloat(water->find("choppiness"), comp.choppiness);
            comp.roughness          = readFloat(water->find("roughness"), comp.roughness);
            comp.foamIntensity      = readFloat(water->find("foamIntensity"), comp.foamIntensity);
            comp.edgeFade           = readFloat(water->find("edgeFade"), comp.edgeFade);
            if (auto* v = water->find("flowDirection")) comp.flowDirection = readVec2(*v);
            comp.flowSpeed          = readFloat(water->find("flowSpeed"), comp.flowSpeed);
            comp.fluidDensity       = readFloat(water->find("fluidDensity"), comp.fluidDensity);
            comp.drag               = readFloat(water->find("drag"), comp.drag);
            comp.buoyancyMultiplier = readFloat(water->find("buoyancyMultiplier"), comp.buoyancyMultiplier);
            comp.affectsRigidBodies = readBool(water->find("affectsRigidBodies"), comp.affectsRigidBodies);
            comp.dirty = true;
            e.addComponent<WaterBodyComponent>() = comp;
        }
    }

    for (const auto& [child, parent] : pendingParents) {
        if (m_scene->entityExists(child) && m_scene->entityExists(parent))
            m_scene->setParent(child, parent);
    }

    DEMON_LOG_INFO("Scene loaded from JSON string");
    return true;
}

bool SceneSerializer::deserialize(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) {
        DEMON_LOG_ERROR("SceneSerializer: cannot open '{}'", filepath);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    bool ok = deserializeFromString(buffer.str());
    if (ok) DEMON_LOG_INFO("Scene loaded: '{}'", filepath);
    return ok;
}

// ----------------------------------------------------------------------------
// Binary Serialization
// ----------------------------------------------------------------------------
bool SceneSerializer::serializeBinary(const std::string& filepath)
{
    BinaryWriter writer;
    if (!writer.open(filepath)) return false;

    SceneHeader header;
    writer.write(header);
    writer.writeString(m_scene->getName());

    const auto& entities = m_scene->getEntities();
    uint32_t count = static_cast<uint32_t>(entities.size());
    writer.write(count);

    for (EntityID id : entities) {
        writer.write(static_cast<uint64_t>(id));
        if (auto* tc = m_scene->getComponent<TagComponent>(id))
            writer.writeString(tc->tag);
        else
            writer.writeString("Entity");
        writer.write(static_cast<uint64_t>(m_scene->getParent(id)));

        uint32_t mask = 0;
        if (m_scene->getComponent<TransformComponent>(id))    mask |= HasTransform;
        if (m_scene->getComponent<MeshRendererComponent>(id)) mask |= HasMesh;
        if (m_scene->getComponent<MaterialComponent>(id))     mask |= HasMaterial;
        if (m_scene->getComponent<CameraComponent>(id))       mask |= HasCamera;
        if (m_scene->getComponent<LightComponent>(id))        mask |= HasLight;
        if (m_scene->getComponent<ScriptComponent>(id))       mask |= HasScript;
        if (m_scene->getComponent<RigidBodyComponent>(id))    mask |= HasRigidBody;
        if (m_scene->getComponent<BoxColliderComponent>(id))  mask |= HasBoxCollider;
        if (m_scene->getComponent<SkyboxComponent>(id))       mask |= HasSkybox;
        if (m_scene->getComponent<FogComponent>(id))          mask |= HasFog;
        if (m_scene->getComponent<TerrainComponent>(id))      mask |= HasTerrain;
        if (m_scene->getComponent<TerrainSculptComponent>(id)) mask |= HasTerrainSculpt;
        if (m_scene->getComponent<TerrainFoliageComponent>(id)) mask |= HasTerrainFoliage;
        if (m_scene->getComponent<WaterBodyComponent>(id))    mask |= HasWaterBody;
        if (m_scene->getComponent<ReflectionProbeComponent>(id)) mask |= HasReflectionProbe;
        if (m_scene->getComponent<AnimatorComponent>(id))     mask |= HasAnimator;
        if (m_scene->getComponent<VolumetricFogComponent>(id)) mask |= HasVolumetricFog;
        if (m_scene->getComponent<VolumetricCloudComponent>(id)) mask |= HasVolumetricCloud;
        if (m_scene->getComponent<LensFlareComponent>(id))    mask |= HasLensFlare;
        if (m_scene->getComponent<UIElementComponent>(id))    mask |= HasUIElement;
        writer.write(mask);

        if (auto* tr = m_scene->getComponent<TransformComponent>(id)) {
            writeVec3Bin(writer, tr->translation);
            writeVec3Bin(writer, tr->rotation);
            writeVec3Bin(writer, tr->scale);
        }

        if (auto* mr = m_scene->getComponent<MeshRendererComponent>(id)) {
            writer.writeString(mr->meshPath);
            writer.writeString(mr->materialPath);
            writer.write(mr->subMeshIndex);
            writer.write(mr->preserveHierarchy);
            writer.write(mr->castShadows);
            writer.write(mr->receiveShadows);
            writer.write(mr->visible);
        }

        if (auto* animator = m_scene->getComponent<AnimatorComponent>(id)) {
            writer.write(animator->playing);
            writer.write(animator->looping);
            writer.write(animator->playbackSpeed);
            writer.write(animator->blendDuration);
            writer.write(animator->blendElapsed);
            writer.write(animator->currentTime);
            writer.write(animator->nextTime);
            writer.writeString(animator->currentClip);
            writer.writeString(animator->nextClip);
        }

        if (auto* mc = m_scene->getComponent<MaterialComponent>(id)) {
            writer.writeString(mc->materialPath);
            writeVec4Bin(writer, mc->albedoColor);
            writer.write(mc->metallic);
            writer.write(mc->roughness);
            writer.write(mc->ao);
            writeVec3Bin(writer, mc->emissiveColor);
            writer.write(mc->emissiveStrength);
            writer.write(mc->doubleSided);
            writer.write(mc->alphaBlend);
            writer.write(mc->alphaCutoff);
            writer.writeString(mc->albedoTexture);
            writer.writeString(mc->normalTexture);
            writer.writeString(mc->metallicTexture);
            writer.writeString(mc->emissiveTexture);
        }

        if (auto* cc = m_scene->getComponent<CameraComponent>(id)) {
            writer.write(cc->primary);
            writer.write(cc->fixedAspect);
            writer.write(cc->camera.getFovY());
            writer.write(cc->camera.getNearClip());
            writer.write(cc->camera.getFarClip());
        }

        if (auto* sb = m_scene->getComponent<SkyboxComponent>(id)) {
            writer.write(sb->enabled);
            writer.writeString(sb->texturePath);
            writer.write(sb->intensity);
        }

        if (auto* fg = m_scene->getComponent<FogComponent>(id)) {
            writer.write(fg->enabled);
            writeVec3Bin(writer, fg->color);
            writer.write(fg->density);
            writer.write(fg->height);
            writer.write(fg->heightFalloff);
            writer.write(fg->start);
        }

        if (auto* vf = m_scene->getComponent<VolumetricFogComponent>(id)) {
            writer.write(vf->enabled);
            writeVec3Bin(writer, vf->color);
            writer.write(vf->density);
            writer.write(vf->intensity);
            writer.write(vf->anisotropy);
            writer.write(vf->height);
            writer.write(vf->heightFalloff);
            writer.write(vf->startDistance);
            writer.write(vf->maxOpacity);
        }

        if (auto* clouds = m_scene->getComponent<VolumetricCloudComponent>(id)) {
            writer.write(clouds->enabled);
            int preset = static_cast<int>(clouds->preset);
            writer.write(preset);
            writer.write(clouds->coverage);
            writer.write(clouds->density);
            writer.write(clouds->altitude);
            writer.write(clouds->thickness);
            writer.write(clouds->scale);
            writer.write(clouds->speed);
            writer.write(clouds->darkness);
            writeVec3Bin(writer, clouds->tint);
        }

        if (auto* flare = m_scene->getComponent<LensFlareComponent>(id)) {
            writer.write(flare->enabled);
            writer.write(flare->intensity);
            writer.write(flare->threshold);
            writer.write(flare->haloWidth);
            writer.write(flare->ghostSpacing);
            writer.write(flare->dirtIntensity);
            writeVec3Bin(writer, flare->tint);
        }

        if (auto* probe = m_scene->getComponent<ReflectionProbeComponent>(id)) {
            writer.write(probe->enabled);
            writer.writeString(probe->assetPath);
            writer.write(probe->priority);
        }

        if (auto* lc = m_scene->getComponent<LightComponent>(id)) {
            int type = static_cast<int>(lc->type);
            writer.write(type);
            writeVec3Bin(writer, lc->color);
            writer.write(lc->intensity);
            writer.write(lc->range);
            writer.write(lc->innerAngle);
            writer.write(lc->outerAngle);
            writer.write(lc->castShadows);
        }

        if (auto* sc = m_scene->getComponent<ScriptComponent>(id)) {
            writer.writeString(sc->className);
            const uint32_t fieldCount = static_cast<uint32_t>(sc->fieldValues.size());
            writer.write(fieldCount);
            for (const ScriptFieldValue& field : sc->fieldValues)
                writeScriptFieldValueBin(writer, field);
        }

        if (auto* rb = m_scene->getComponent<RigidBodyComponent>(id)) {
            int type = static_cast<int>(rb->type);
            writer.write(type);
            writer.write(rb->mass);
            writer.write(rb->linearDamping);
            writer.write(rb->angularDamping);
            writer.write(rb->useGravity);
            writer.write(rb->gravityScale);
            writer.write(rb->isKinematic);
            writer.write(rb->simulatePhysics);
            writer.write(rb->lockRotation);
            writer.write(rb->lockPositionX);
            writer.write(rb->lockPositionY);
            writer.write(rb->lockPositionZ);
            writer.write(rb->lockRotationX);
            writer.write(rb->lockRotationY);
            writer.write(rb->lockRotationZ);
            writer.write(rb->continuousCollision);
            writer.write(rb->allowSleeping);
            writer.write(rb->collisionLayer);
            writeVec3Bin(writer, rb->linearVelocity);
            writeVec3Bin(writer, rb->angularVelocity);
        }

        if (auto* bc = m_scene->getComponent<BoxColliderComponent>(id)) {
            writeVec3Bin(writer, bc->halfExtents);
            writeVec3Bin(writer, bc->offset);
            writer.write(bc->friction);
            writer.write(bc->restitution);
            writer.write(bc->isTrigger);
        }

        if (auto* ui = m_scene->getComponent<UIElementComponent>(id)) {
            uint8_t kind = static_cast<uint8_t>(ui->kind);
            uint8_t shape = static_cast<uint8_t>(ui->shape);
            writer.write(kind);
            writer.write(shape);
            writer.write(ui->visible);
            writer.write(ui->screenSpace);
            writer.write(ui->billboard);
            writer.writeString(ui->text);
            writer.writeString(ui->imagePath);
            writeVec4Bin(writer, ui->color);
            writeVec2Bin(writer, ui->size);
            writer.write(ui->fontSize);
            writer.write(ui->depth);
        }

        if (auto* terrain = m_scene->getComponent<TerrainComponent>(id)) {
            writer.write(terrain->resolution);
            writer.write(terrain->sizeX);
            writer.write(terrain->sizeZ);
            writer.write(terrain->maxHeight);
            writer.write(terrain->uvScale);
            writeVec4Bin(writer, terrain->lowColor);
            writeVec4Bin(writer, terrain->midColor);
            writeVec4Bin(writer, terrain->highColor);
            writer.write(terrain->castShadows);
            writer.write(terrain->receiveShadows);
            writer.write(terrain->collisionEnabled);
            const uint32_t heightCount = static_cast<uint32_t>(terrain->heights.size());
            writer.write(heightCount);
            for (float h : terrain->heights)
                writer.write(h);
        }

        if (auto* sculpt = m_scene->getComponent<TerrainSculptComponent>(id)) {
            const uint32_t tool = static_cast<uint32_t>(sculpt->tool);
            writer.write(tool);
            writer.write(sculpt->brushRadius);
            writer.write(sculpt->brushStrength);
            writer.write(sculpt->brushFalloff);
            writer.write(sculpt->flattenTarget);
            writer.write(sculpt->noiseScale);
            writer.write(sculpt->terraceSpacing);
            writer.write(sculpt->erosionAmount);
            writer.write(sculpt->sharpenAmount);
            writeVec2Bin(writer, sculpt->brushCenter);
            writer.write(sculpt->autoRebuild);
        }

        if (auto* foliage = m_scene->getComponent<TerrainFoliageComponent>(id)) {
            writer.write(foliage->treesEnabled);
            writer.write(foliage->grassEnabled);
            writer.write(foliage->treeCount);
            writer.write(foliage->grassCount);
            writer.writeString(foliage->treeMeshPath);
            writer.writeString(foliage->grassMeshPath);
            writer.write(foliage->treeMinScale);
            writer.write(foliage->treeMaxScale);
            writer.write(foliage->grassMinScale);
            writer.write(foliage->grassMaxScale);
            writer.write(foliage->placementJitter);
            writer.write(foliage->brushRadius);
            writer.write(foliage->brushDensity);
            writeVec2Bin(writer, foliage->brushCenter);
            writer.write(foliage->minHeight);
            writer.write(foliage->maxHeight);
            writer.write(foliage->maxSlopeDegrees);
            writer.write(foliage->randomSeed);
            writeVec4Bin(writer, foliage->treeTrunkColor);
            writeVec4Bin(writer, foliage->treeLeafColor);
            writeVec4Bin(writer, foliage->grassColor);
            const uint32_t paintedTreeCount = static_cast<uint32_t>(foliage->paintedTrees.size());
            writer.write(paintedTreeCount);
            for (const glm::vec4& instance : foliage->paintedTrees)
                writeVec4Bin(writer, instance);
            const uint32_t paintedGrassCount = static_cast<uint32_t>(foliage->paintedGrass.size());
            writer.write(paintedGrassCount);
            for (const glm::vec4& instance : foliage->paintedGrass)
                writeVec4Bin(writer, instance);
        }

        if (auto* water = m_scene->getComponent<WaterBodyComponent>(id)) {
            const uint32_t type = static_cast<uint32_t>(water->type);
            writer.write(type);
            writer.write(water->resolution);
            writeVec2Bin(writer, water->size);
            writer.write(water->depth);
            writeVec4Bin(writer, water->surfaceColor);
            writeVec4Bin(writer, water->bottomColor);
            writer.write(water->transparency);
            writer.write(water->waveAmplitude);
            writer.write(water->waveLength);
            writer.write(water->waveSpeed);
            writer.write(water->choppiness);
            writer.write(water->roughness);
            writer.write(water->foamIntensity);
            writer.write(water->edgeFade);
            writeVec2Bin(writer, water->flowDirection);
            writer.write(water->flowSpeed);
            writer.write(water->fluidDensity);
            writer.write(water->drag);
            writer.write(water->buoyancyMultiplier);
            writer.write(water->affectsRigidBodies);
        }
    }

    DEMON_LOG_INFO("Scene saved (binary): '{}'", filepath);
    return true;
}

bool SceneSerializer::deserializeBinary(const std::string& filepath)
{
    BinaryReader reader;
    if (!reader.open(filepath)) return false;

    SceneHeader header{};
    if (!reader.read(header)) return false;
    if (header.magic != kSceneMagic || header.version == 0 || header.version > kSceneVersion) {
        DEMON_LOG_ERROR("SceneSerializer: binary header mismatch in '{}'", filepath);
        return false;
    }

    std::string sceneName;
    if (!reader.readString(sceneName)) return false;
    m_scene->setName(sceneName);

    uint32_t count = 0;
    if (!reader.read(count)) return false;

    std::vector<std::pair<EntityID, EntityID>> pendingParents;

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t id = 0;
        if (!reader.read(id)) return false;

        std::string tag;
        if (!reader.readString(tag)) return false;

        uint64_t parentId = 0;
        if (header.version >= 2) {
            if (!reader.read(parentId)) return false;
        }

        uint32_t mask = 0;
        if (!reader.read(mask)) return false;

        Entity e = m_scene->createEntityWithID(UUID(id), tag);
        if (parentId != 0)
            pendingParents.emplace_back(id, parentId);

        if (mask & HasTransform) {
            TransformComponent comp;
            readVec3Bin(reader, comp.translation);
            readVec3Bin(reader, comp.rotation);
            readVec3Bin(reader, comp.scale);
            e.addComponent<TransformComponent>() = comp;
        }

        if (mask & HasMesh) {
            MeshRendererComponent comp;
            reader.readString(comp.meshPath);
            reader.readString(comp.materialPath);
            if (header.version >= 4) {
                reader.read(comp.subMeshIndex);
                reader.read(comp.preserveHierarchy);
            }
            reader.read(comp.castShadows);
            reader.read(comp.receiveShadows);
            reader.read(comp.visible);
            comp.meshHandle = 0;
            comp.materialHandle = 0;
            e.addComponent<MeshRendererComponent>() = comp;
        }

        if ((header.version >= 9) && (mask & HasAnimator)) {
            AnimatorComponent comp;
            reader.read(comp.playing);
            reader.read(comp.looping);
            reader.read(comp.playbackSpeed);
            reader.read(comp.blendDuration);
            reader.read(comp.blendElapsed);
            reader.read(comp.currentTime);
            reader.read(comp.nextTime);
            reader.readString(comp.currentClip);
            reader.readString(comp.nextClip);
            e.addComponent<AnimatorComponent>() = std::move(comp);
        }

        if (mask & HasMaterial) {
            MaterialComponent comp;
            reader.readString(comp.materialPath);
            readVec4Bin(reader, comp.albedoColor);
            reader.read(comp.metallic);
            reader.read(comp.roughness);
            reader.read(comp.ao);
            readVec3Bin(reader, comp.emissiveColor);
            reader.read(comp.emissiveStrength);
            reader.read(comp.doubleSided);
            reader.read(comp.alphaBlend);
            reader.read(comp.alphaCutoff);
            reader.readString(comp.albedoTexture);
            reader.readString(comp.normalTexture);
            reader.readString(comp.metallicTexture);
            reader.readString(comp.emissiveTexture);
            comp.dirty = true;
            e.addComponent<MaterialComponent>() = comp;
        }

        if (mask & HasCamera) {
            CameraComponent comp;
            reader.read(comp.primary);
            reader.read(comp.fixedAspect);
            float fovY = 0.0f;
            float nearC = 0.0f;
            float farC = 0.0f;
            reader.read(fovY);
            reader.read(nearC);
            reader.read(farC);
            comp.camera.setPerspective(fovY, 16.0f / 9.0f, nearC, farC);
            e.addComponent<CameraComponent>() = comp;
        }

        if ((header.version >= 3) && (mask & HasSkybox)) {
            SkyboxComponent comp;
            reader.read(comp.enabled);
            reader.readString(comp.texturePath);
            reader.read(comp.intensity);
            e.addComponent<SkyboxComponent>() = comp;
        }

        if ((header.version >= 3) && (mask & HasFog)) {
            FogComponent comp;
            reader.read(comp.enabled);
            readVec3Bin(reader, comp.color);
            reader.read(comp.density);
            reader.read(comp.height);
            reader.read(comp.heightFalloff);
            reader.read(comp.start);
            e.addComponent<FogComponent>() = comp;
        }

        if ((header.version >= 12) && (mask & HasVolumetricFog)) {
            VolumetricFogComponent comp;
            reader.read(comp.enabled);
            readVec3Bin(reader, comp.color);
            reader.read(comp.density);
            reader.read(comp.intensity);
            reader.read(comp.anisotropy);
            reader.read(comp.height);
            reader.read(comp.heightFalloff);
            reader.read(comp.startDistance);
            reader.read(comp.maxOpacity);
            e.addComponent<VolumetricFogComponent>() = comp;
        }

        if ((header.version >= 12) && (mask & HasVolumetricCloud)) {
            VolumetricCloudComponent comp;
            reader.read(comp.enabled);
            int preset = static_cast<int>(comp.preset);
            reader.read(preset);
            comp.preset = static_cast<VolumetricCloudPreset>(preset);
            reader.read(comp.coverage);
            reader.read(comp.density);
            reader.read(comp.altitude);
            reader.read(comp.thickness);
            reader.read(comp.scale);
            reader.read(comp.speed);
            reader.read(comp.darkness);
            readVec3Bin(reader, comp.tint);
            e.addComponent<VolumetricCloudComponent>() = comp;
        }

        if ((header.version >= 12) && (mask & HasLensFlare)) {
            LensFlareComponent comp;
            reader.read(comp.enabled);
            reader.read(comp.intensity);
            reader.read(comp.threshold);
            reader.read(comp.haloWidth);
            reader.read(comp.ghostSpacing);
            reader.read(comp.dirtIntensity);
            readVec3Bin(reader, comp.tint);
            e.addComponent<LensFlareComponent>() = comp;
        }

        if ((header.version >= 7) && (mask & HasReflectionProbe)) {
            ReflectionProbeComponent comp;
            reader.read(comp.enabled);
            reader.readString(comp.assetPath);
            reader.read(comp.priority);
            e.addComponent<ReflectionProbeComponent>() = comp;
        }

        if (mask & HasLight) {
            LightComponent comp;
            int type = static_cast<int>(comp.type);
            reader.read(type);
            comp.type = static_cast<LightType>(type);
            readVec3Bin(reader, comp.color);
            reader.read(comp.intensity);
            reader.read(comp.range);
            reader.read(comp.innerAngle);
            reader.read(comp.outerAngle);
            reader.read(comp.castShadows);
            e.addComponent<LightComponent>() = comp;
        }

        if (mask & HasScript) {
            ScriptComponent comp;
            reader.readString(comp.className);
            if (header.version >= 5) {
                uint32_t fieldCount = 0;
                reader.read(fieldCount);
                comp.fieldValues.reserve(fieldCount);
                for (uint32_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
                    ScriptFieldValue field;
                    if (!readScriptFieldValueBin(reader, field))
                        return false;
                    comp.fieldValues.push_back(std::move(field));
                }
            }
            e.addComponent<ScriptComponent>() = comp;
        }

        if (mask & HasRigidBody) {
            RigidBodyComponent comp;
            int type = static_cast<int>(comp.type);
            reader.read(type);
            comp.type = static_cast<BodyType>(type);
            reader.read(comp.mass);
            reader.read(comp.linearDamping);
            reader.read(comp.angularDamping);
            reader.read(comp.useGravity);
            if (header.version >= 6) {
                reader.read(comp.gravityScale);
                reader.read(comp.isKinematic);
                reader.read(comp.simulatePhysics);
                reader.read(comp.lockRotation);
                if (header.version >= 13) {
                    reader.read(comp.lockPositionX);
                    reader.read(comp.lockPositionY);
                    reader.read(comp.lockPositionZ);
                    reader.read(comp.lockRotationX);
                    reader.read(comp.lockRotationY);
                    reader.read(comp.lockRotationZ);
                    reader.read(comp.continuousCollision);
                    reader.read(comp.allowSleeping);
                    reader.read(comp.collisionLayer);
                }
                readVec3Bin(reader, comp.linearVelocity);
                readVec3Bin(reader, comp.angularVelocity);
            } else {
                reader.read(comp.isKinematic);
            }
            e.addComponent<RigidBodyComponent>() = comp;
        }

        if (mask & HasBoxCollider) {
            BoxColliderComponent comp;
            readVec3Bin(reader, comp.halfExtents);
            readVec3Bin(reader, comp.offset);
            reader.read(comp.friction);
            reader.read(comp.restitution);
            reader.read(comp.isTrigger);
            e.addComponent<BoxColliderComponent>() = comp;
        }

        if ((header.version >= 14) && (mask & HasUIElement)) {
            UIElementComponent comp;
            uint8_t kind = 0;
            uint8_t shape = 0;
            reader.read(kind);
            reader.read(shape);
            comp.kind = static_cast<UIElementKind>(kind);
            comp.shape = static_cast<UIShapeKind>(shape);
            reader.read(comp.visible);
            reader.read(comp.screenSpace);
            reader.read(comp.billboard);
            reader.readString(comp.text);
            reader.readString(comp.imagePath);
            readVec4Bin(reader, comp.color);
            readVec2Bin(reader, comp.size);
            reader.read(comp.fontSize);
            reader.read(comp.depth);
            e.addComponent<UIElementComponent>() = comp;
        }

        if ((header.version >= 6) && (mask & HasTerrain)) {
            TerrainComponent comp;
            reader.read(comp.resolution);
            reader.read(comp.sizeX);
            reader.read(comp.sizeZ);
            reader.read(comp.maxHeight);
            reader.read(comp.uvScale);
            readVec4Bin(reader, comp.lowColor);
            readVec4Bin(reader, comp.midColor);
            readVec4Bin(reader, comp.highColor);
            reader.read(comp.castShadows);
            reader.read(comp.receiveShadows);
            reader.read(comp.collisionEnabled);
            uint32_t heightCount = 0;
            reader.read(heightCount);
            comp.heights.resize(heightCount);
            for (uint32_t heightIndex = 0; heightIndex < heightCount; ++heightIndex)
                reader.read(comp.heights[heightIndex]);
            comp.dirty = true;
            e.addComponent<TerrainComponent>() = comp;
        }

        if ((header.version >= 6) && (mask & HasTerrainSculpt)) {
            TerrainSculptComponent comp;
            uint32_t tool = static_cast<uint32_t>(comp.tool);
            reader.read(tool);
            comp.tool = static_cast<TerrainSculptTool>(tool);
            reader.read(comp.brushRadius);
            reader.read(comp.brushStrength);
            reader.read(comp.brushFalloff);
            reader.read(comp.flattenTarget);
            if (header.version >= 8) {
                reader.read(comp.noiseScale);
                reader.read(comp.terraceSpacing);
                reader.read(comp.erosionAmount);
                reader.read(comp.sharpenAmount);
            }
            readVec2Bin(reader, comp.brushCenter);
            reader.read(comp.autoRebuild);
            e.addComponent<TerrainSculptComponent>() = comp;
        }

        if ((header.version >= 6) && (mask & HasTerrainFoliage)) {
            TerrainFoliageComponent comp;
            reader.read(comp.treesEnabled);
            reader.read(comp.grassEnabled);
            reader.read(comp.treeCount);
            reader.read(comp.grassCount);
            if (header.version >= 11) {
                reader.readString(comp.treeMeshPath);
                reader.readString(comp.grassMeshPath);
            }
            reader.read(comp.treeMinScale);
            reader.read(comp.treeMaxScale);
            reader.read(comp.grassMinScale);
            reader.read(comp.grassMaxScale);
            reader.read(comp.placementJitter);
            if (header.version >= 11) {
                reader.read(comp.brushRadius);
                reader.read(comp.brushDensity);
                readVec2Bin(reader, comp.brushCenter);
            }
            reader.read(comp.minHeight);
            reader.read(comp.maxHeight);
            reader.read(comp.maxSlopeDegrees);
            reader.read(comp.randomSeed);
            readVec4Bin(reader, comp.treeTrunkColor);
            readVec4Bin(reader, comp.treeLeafColor);
            readVec4Bin(reader, comp.grassColor);
            if (header.version >= 11) {
                uint32_t paintedTreeCount = 0;
                reader.read(paintedTreeCount);
                comp.paintedTrees.reserve(paintedTreeCount);
                for (uint32_t i = 0; i < paintedTreeCount; ++i) {
                    glm::vec4 instance{};
                    readVec4Bin(reader, instance);
                    comp.paintedTrees.push_back(instance);
                }

                uint32_t paintedGrassCount = 0;
                reader.read(paintedGrassCount);
                comp.paintedGrass.reserve(paintedGrassCount);
                for (uint32_t i = 0; i < paintedGrassCount; ++i) {
                    glm::vec4 instance{};
                    readVec4Bin(reader, instance);
                    comp.paintedGrass.push_back(instance);
                }
            }
            comp.dirty = true;
            e.addComponent<TerrainFoliageComponent>() = comp;
        }

        if ((header.version >= 6) && (mask & HasWaterBody)) {
            WaterBodyComponent comp;
            uint32_t type = static_cast<uint32_t>(comp.type);
            reader.read(type);
            comp.type = static_cast<WaterBodyType>(type);
            reader.read(comp.resolution);
            readVec2Bin(reader, comp.size);
            reader.read(comp.depth);
            readVec4Bin(reader, comp.surfaceColor);
            readVec4Bin(reader, comp.bottomColor);
            reader.read(comp.transparency);
            reader.read(comp.waveAmplitude);
            reader.read(comp.waveLength);
            reader.read(comp.waveSpeed);
            if (header.version >= 10) {
                reader.read(comp.choppiness);
                reader.read(comp.roughness);
                reader.read(comp.foamIntensity);
                reader.read(comp.edgeFade);
            }
            readVec2Bin(reader, comp.flowDirection);
            reader.read(comp.flowSpeed);
            reader.read(comp.fluidDensity);
            reader.read(comp.drag);
            reader.read(comp.buoyancyMultiplier);
            reader.read(comp.affectsRigidBodies);
            comp.dirty = true;
            e.addComponent<WaterBodyComponent>() = comp;
        }
    }

    for (const auto& [child, parent] : pendingParents) {
        if (m_scene->entityExists(child) && m_scene->entityExists(parent))
            m_scene->setParent(child, parent);
    }

    DEMON_LOG_INFO("Scene loaded (binary): '{}'", filepath);
    return true;
}

} // namespace Demon
