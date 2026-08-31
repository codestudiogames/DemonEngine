// ==============================================================================
//  DemonEngine Editor::PropertiesPanel  –  Implementation
// ==============================================================================
#include "PropertiesPanel.h"
#include <imgui.h>
#include "core/Logger.h"
#include "renderer/Mesh.h"
#include "scene/Components.h"
#include "scripting/ScriptEngine.h"
#include "MaterialEditorPanel.h"
#include "../EditorSettings.h"
#include "../utils/FileDialogs.h"
#include "../utils/TextBuffer.h"

#ifndef ImGuiTreeNodeFlags_AllowItemOverlap
#define ImGuiTreeNodeFlags_AllowItemOverlap ImGuiTreeNodeFlags_AllowOverlap
#endif

namespace Demon {

namespace {

const char* cloudPresetName(VolumetricCloudPreset preset)
{
    switch (preset) {
        case VolumetricCloudPreset::Clear: return "Clear";
        case VolumetricCloudPreset::FewClouds: return "Few Clouds";
        case VolumetricCloudPreset::Cloudy: return "Cloudy";
        case VolumetricCloudPreset::Overcast: return "Overcast";
        case VolumetricCloudPreset::Thunder: return "Thunder";
        case VolumetricCloudPreset::Sunset: return "Sunset";
        case VolumetricCloudPreset::Storm: return "Storm";
        default: return "Cloudy";
    }
}

void applyCloudPreset(VolumetricCloudComponent& clouds, VolumetricCloudPreset preset)
{
    clouds.preset = preset;
    switch (preset) {
        case VolumetricCloudPreset::Clear:
            clouds.coverage = 0.08f; clouds.density = 0.18f; clouds.darkness = 0.02f; clouds.tint = {1.0f, 1.0f, 1.0f};
            break;
        case VolumetricCloudPreset::FewClouds:
            clouds.coverage = 0.28f; clouds.density = 0.34f; clouds.darkness = 0.08f; clouds.tint = {1.0f, 0.99f, 0.95f};
            break;
        case VolumetricCloudPreset::Cloudy:
            clouds.coverage = 0.48f; clouds.density = 0.52f; clouds.darkness = 0.22f; clouds.tint = {1.0f, 0.98f, 0.92f};
            break;
        case VolumetricCloudPreset::Overcast:
            clouds.coverage = 0.70f; clouds.density = 0.76f; clouds.darkness = 0.42f; clouds.tint = {0.82f, 0.86f, 0.90f};
            break;
        case VolumetricCloudPreset::Thunder:
            clouds.coverage = 0.78f; clouds.density = 0.92f; clouds.darkness = 0.72f; clouds.tint = {0.32f, 0.34f, 0.38f};
            break;
        case VolumetricCloudPreset::Sunset:
            clouds.coverage = 0.42f; clouds.density = 0.50f; clouds.darkness = 0.18f; clouds.tint = {1.0f, 0.70f, 0.46f};
            break;
        case VolumetricCloudPreset::Storm:
            clouds.coverage = 0.86f; clouds.density = 1.05f; clouds.darkness = 0.82f; clouds.tint = {0.24f, 0.27f, 0.32f};
            break;
    }
}

uint32_t sanitizeTerrainResolution(uint32_t resolution)
{
    return std::clamp(resolution == 0 ? 65u : resolution, 2u, 257u);
}

void ensureTerrainData(TerrainComponent& terrain)
{
    terrain.resolution = sanitizeTerrainResolution(terrain.resolution);
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

float sampleTerrainHeight(const TerrainComponent& terrain, float u, float v)
{
    if (terrain.heights.empty())
        return 0.0f;

    const uint32_t resolution = sanitizeTerrainResolution(terrain.resolution);
    const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(resolution - 1);
    const float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(resolution - 1);
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
    return std::lerp(std::lerp(h00, h10, tx), std::lerp(h01, h11, tx), tz);
}

void smoothTerrain(TerrainComponent& terrain)
{
    ensureTerrainData(terrain);
    std::vector<float> smoothed = terrain.heights;

    for (uint32_t z = 0; z < terrain.resolution; ++z) {
        for (uint32_t x = 0; x < terrain.resolution; ++x) {
            float total = 0.0f;
            float weight = 0.0f;
            for (int oz = -1; oz <= 1; ++oz) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const uint32_t sx = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(x) + ox, 0, static_cast<int>(terrain.resolution) - 1));
                    const uint32_t sz = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(z) + oz, 0, static_cast<int>(terrain.resolution) - 1));
                    const float kernel = (ox == 0 && oz == 0) ? 2.0f : 1.0f;
                    total += terrain.heights[terrainIndex(terrain, sx, sz)] * kernel;
                    weight += kernel;
                }
            }
            smoothed[terrainIndex(terrain, x, z)] = total / std::max(weight, 0.001f);
        }
    }

    terrain.heights = std::move(smoothed);
    terrain.dirty = true;
}

void applyTerrainBrush(TerrainComponent& terrain, const TerrainSculptComponent& sculpt)
{
    ensureTerrainData(terrain);
    const float centerX = (sculpt.brushCenter.x - 0.5f) * terrain.sizeX;
    const float centerZ = (sculpt.brushCenter.y - 0.5f) * terrain.sizeZ;
    const float radius = std::max(sculpt.brushRadius, 0.1f);
    const float strength = std::max(sculpt.brushStrength, 0.0f);
    std::vector<float> source = terrain.heights;
    auto sampleAverage = [&](uint32_t x, uint32_t z) {
        float total = 0.0f;
        float count = 0.0f;
        for (int oz = -1; oz <= 1; ++oz) {
            for (int ox = -1; ox <= 1; ++ox) {
                const uint32_t sx = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(x) + ox, 0, static_cast<int>(terrain.resolution) - 1));
                const uint32_t sz = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(z) + oz, 0, static_cast<int>(terrain.resolution) - 1));
                total += source[terrainIndex(terrain, sx, sz)];
                count += 1.0f;
            }
        }
        return total / std::max(count, 1.0f);
    };

    for (uint32_t z = 0; z < terrain.resolution; ++z) {
        for (uint32_t x = 0; x < terrain.resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max<int>(1, static_cast<int>(terrain.resolution) - 1));
            const float v = static_cast<float>(z) / static_cast<float>(std::max<int>(1, static_cast<int>(terrain.resolution) - 1));
            const float localX = (u - 0.5f) * terrain.sizeX;
            const float localZ = (v - 0.5f) * terrain.sizeZ;
            const float distance = glm::length(glm::vec2(localX - centerX, localZ - centerZ));
            if (distance > radius)
                continue;

            const float falloff = std::pow(1.0f - (distance / radius), std::max(sculpt.brushFalloff, 0.01f));
            float& height = terrain.heights[terrainIndex(terrain, x, z)];

            switch (sculpt.tool) {
                case TerrainSculptTool::Raise:
                    height += strength * falloff;
                    break;
                case TerrainSculptTool::Lower:
                    height -= strength * falloff;
                    break;
                case TerrainSculptTool::Flatten:
                    height = std::lerp(height, sculpt.flattenTarget, std::clamp(falloff * strength * 0.2f, 0.0f, 1.0f));
                    break;
                case TerrainSculptTool::Smooth: {
                    const float average = sampleAverage(x, z);
                    height = std::lerp(height, average, std::clamp(falloff * strength * 0.18f, 0.0f, 1.0f));
                    break;
                }
                case TerrainSculptTool::Noise: {
                    const float wave = std::sin(localX * sculpt.noiseScale) +
                                       std::cos(localZ * (sculpt.noiseScale * 1.23f));
                    height += wave * strength * falloff * 0.35f;
                    break;
                }
                case TerrainSculptTool::Terrace: {
                    const float terraceSpacing = std::max(sculpt.terraceSpacing, 0.1f);
                    const float targetHeight = std::round(height / terraceSpacing) * terraceSpacing;
                    height = std::lerp(height, targetHeight,
                                       std::clamp(falloff * strength * 0.18f, 0.0f, 1.0f));
                    break;
                }
                case TerrainSculptTool::Erode: {
                    const float average = sampleAverage(x, z);
                    const float delta = height - average;
                    height -= delta * std::clamp(falloff * sculpt.erosionAmount * strength * 0.12f, 0.0f, 1.0f);
                    break;
                }
                case TerrainSculptTool::Sharpen: {
                    const float average = sampleAverage(x, z);
                    const float delta = height - average;
                    height += delta * std::clamp(falloff * sculpt.sharpenAmount * strength * 0.10f, 0.0f, 1.0f);
                    break;
                }
            }

            height = std::clamp(height, 0.0f, terrain.maxHeight);
        }
    }

    terrain.dirty = true;
}

void fillTerrainNoise(TerrainComponent& terrain)
{
    ensureTerrainData(terrain);
    for (uint32_t z = 0; z < terrain.resolution; ++z) {
        for (uint32_t x = 0; x < terrain.resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(std::max<int>(1, static_cast<int>(terrain.resolution) - 1));
            const float v = static_cast<float>(z) / static_cast<float>(std::max<int>(1, static_cast<int>(terrain.resolution) - 1));
            const float ridge = std::sin(u * 8.0f) * std::cos(v * 7.0f);
            const float detail = std::sin((u + v) * 19.0f) * 0.15f;
            terrain.heights[terrainIndex(terrain, x, z)] =
                std::clamp((0.5f + ridge * 0.35f + detail) * terrain.maxHeight * 0.6f, 0.0f, terrain.maxHeight);
        }
    }
    terrain.dirty = true;
}

void normalizeTerrainHeights(TerrainComponent& terrain)
{
    ensureTerrainData(terrain);
    if (terrain.heights.empty())
        return;

    const auto [minIt, maxIt] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
    const float minHeight = *minIt;
    const float maxHeight = *maxIt;
    const float range = maxHeight - minHeight;
    if (range <= 1e-5f)
        return;

    for (float& height : terrain.heights)
        height = ((height - minHeight) / range) * terrain.maxHeight;
    terrain.dirty = true;
}

void invertTerrainHeights(TerrainComponent& terrain)
{
    ensureTerrainData(terrain);
    for (float& height : terrain.heights)
        height = std::clamp(terrain.maxHeight - height, 0.0f, terrain.maxHeight);
    terrain.dirty = true;
}

void applyIslandMask(TerrainComponent& terrain)
{
    ensureTerrainData(terrain);
    const float invLast = 1.0f / static_cast<float>(std::max<int>(1, static_cast<int>(terrain.resolution) - 1));
    for (uint32_t z = 0; z < terrain.resolution; ++z) {
        for (uint32_t x = 0; x < terrain.resolution; ++x) {
            const float u = static_cast<float>(x) * invLast;
            const float v = static_cast<float>(z) * invLast;
            const float dx = (u - 0.5f) * 2.0f;
            const float dz = (v - 0.5f) * 2.0f;
            const float radial = std::sqrt(dx * dx + dz * dz);
            const float mask = std::pow(std::clamp(1.0f - radial, 0.0f, 1.0f), 1.65f);
            terrain.heights[terrainIndex(terrain, x, z)] =
                std::clamp(terrain.heights[terrainIndex(terrain, x, z)] * mask, 0.0f, terrain.maxHeight);
        }
    }
    terrain.dirty = true;
}

bool isModelPath(std::string_view path)
{
    std::filesystem::path p{std::string(path)};
    std::string ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae";
}

glm::vec3 sampleTerrainNormal(const TerrainComponent& terrain, float u, float v)
{
    const uint32_t resolution = sanitizeTerrainResolution(terrain.resolution);
    const float du = 1.0f / static_cast<float>(std::max<int>(1, static_cast<int>(resolution) - 1));
    const float dv = du;
    const float hl = sampleTerrainHeight(terrain, u - du, v);
    const float hr = sampleTerrainHeight(terrain, u + du, v);
    const float hd = sampleTerrainHeight(terrain, u, v - dv);
    const float hu = sampleTerrainHeight(terrain, u, v + dv);
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

bool drawModelPathInput(const char* label, std::string& path)
{
    bool changed = false;
    char buf[512];
    copyStringToBuffer(buf, path);
    if (ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)
        || ImGui::IsItemDeactivatedAfterEdit()) {
        path = buf;
        changed = true;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char* droppedPath = static_cast<const char*>(payload->Data);
            if (droppedPath && isModelPath(droppedPath)) {
                path = droppedPath;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    return changed;
}

void paintFoliageBrush(TerrainComponent& terrain,
                       TerrainFoliageComponent& foliage,
                       bool paintTrees)
{
    ensureTerrainData(terrain);

    const float radius = std::max(foliage.brushRadius, 0.1f);
    const float density = std::max(foliage.brushDensity, 0.01f);
    const float terrainArea = std::max(terrain.sizeX * terrain.sizeZ, 1.0f);
    const float brushArea = glm::pi<float>() * radius * radius;
    const uint32_t count = std::clamp(
        static_cast<uint32_t>(std::round((brushArea / terrainArea) * density * (paintTrees ? 240.0f : 2400.0f))),
        1u,
        paintTrees ? 128u : 1024u);

    const uint32_t salt = paintTrees ? 0x9E3779B9u : 0x85EBCA6Bu;
    std::mt19937 rng(foliage.randomSeed + salt + static_cast<uint32_t>(paintTrees ? foliage.paintedTrees.size() : foliage.paintedGrass.size()));
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    auto& instances = paintTrees ? foliage.paintedTrees : foliage.paintedGrass;
    const float minScale = paintTrees ? foliage.treeMinScale : foliage.grassMinScale;
    const float maxScale = std::max(paintTrees ? foliage.treeMaxScale : foliage.grassMaxScale, minScale);

    for (uint32_t i = 0; i < count; ++i) {
        const float angle = angleDist(rng);
        const float r = std::sqrt(radiusDist(rng)) * radius;
        const float localX = (foliage.brushCenter.x - 0.5f) * terrain.sizeX + std::cos(angle) * r;
        const float localZ = (foliage.brushCenter.y - 0.5f) * terrain.sizeZ + std::sin(angle) * r;
        const float u = std::clamp(localX / terrain.sizeX + 0.5f, 0.0f, 1.0f);
        const float v = std::clamp(localZ / terrain.sizeZ + 0.5f, 0.0f, 1.0f);

        const float height = sampleTerrainHeight(terrain, u, v);
        const float height01 = std::clamp(height / std::max(terrain.maxHeight, 0.001f), 0.0f, 1.0f);
        if (height01 < foliage.minHeight || height01 > foliage.maxHeight)
            continue;

        const glm::vec3 normal = sampleTerrainNormal(terrain, u, v);
        const float slopeDegrees = glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)));
        if (slopeDegrees > foliage.maxSlopeDegrees)
            continue;

        const float scale = std::lerp(minScale, maxScale, unit(rng));
        instances.push_back({u, v, scale, angleDist(rng)});
    }

    foliage.dirty = true;
}

void clearFoliageBrush(TerrainComponent& terrain,
                       TerrainFoliageComponent& foliage)
{
    const float radius = std::max(foliage.brushRadius, 0.1f);
    auto removeInsideBrush = [&](std::vector<glm::vec4>& instances) {
        instances.erase(
            std::remove_if(instances.begin(), instances.end(), [&](const glm::vec4& instance) {
                const float localX = (instance.x - foliage.brushCenter.x) * terrain.sizeX;
                const float localZ = (instance.y - foliage.brushCenter.y) * terrain.sizeZ;
                return glm::length(glm::vec2(localX, localZ)) <= radius;
            }),
            instances.end());
    };

    removeInsideBrush(foliage.paintedTrees);
    removeInsideBrush(foliage.paintedGrass);
    foliage.dirty = true;
}

const char* terrainToolLabel(TerrainSculptTool tool)
{
    switch (tool) {
        case TerrainSculptTool::Raise:   return "Raise";
        case TerrainSculptTool::Lower:   return "Lower";
        case TerrainSculptTool::Flatten: return "Flatten";
        case TerrainSculptTool::Smooth:  return "Smooth";
        case TerrainSculptTool::Noise:   return "Noise";
        case TerrainSculptTool::Terrace: return "Terrace";
        case TerrainSculptTool::Erode:   return "Erode";
        case TerrainSculptTool::Sharpen: return "Sharpen";
        default:                         return "Raise";
    }
}

const char* waterTypeLabel(WaterBodyType type)
{
    switch (type) {
        case WaterBodyType::Lake:       return "Lake";
        case WaterBodyType::River:      return "River";
        case WaterBodyType::Ocean:      return "Ocean";
        case WaterBodyType::Pool:       return "Pool";
        case WaterBodyType::CustomArea: return "Custom Area";
        default:                        return "Lake";
    }
}

} // namespace

// ── Helper to draw a collapsing component header with a remove button ─────────
template<typename T>
void PropertiesPanel::drawComponent(const char* label, Entity e,
                                     std::function<void(T&)> uiFn) {
    if (!e.hasComponent<T>()) return;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_Framed |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_AllowItemOverlap;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    bool open = ImGui::TreeNodeEx(label, flags);
    ImGui::PopStyleVar();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
    if (ImGui::SmallButton("×")) {
        e.removeComponent<T>();
        markEdited();
        if (open) ImGui::TreePop();
        return;
    }

    if (open) {
        uiFn(e.getComponent<T>());
        ImGui::TreePop();
    }
    ImGui::Spacing();
}

// ─────────────────────────────────────────────────────────────────────────────
void PropertiesPanel::render(Entity selectedEntity) {
    ImGui::Begin("Properties", nullptr, editorPanelFlags());

    if (!selectedEntity) {
        ImGui::TextDisabled("No entity selected.");
        ImGui::End();
        return;
    }

    drawTagComponent(selectedEntity);
    ImGui::Separator();

    drawTransformComponent(selectedEntity);
    drawMeshRendererComponent(selectedEntity);
    drawAnimatorComponent(selectedEntity);
    drawMaterialComponent(selectedEntity);
    drawCameraComponent(selectedEntity);
    drawLightComponent(selectedEntity);
    drawSkyboxComponent(selectedEntity);
    drawFogComponent(selectedEntity);
    drawVolumetricFogComponent(selectedEntity);
    drawLocalVolumetricFogComponent(selectedEntity);
    drawVolumetricCloudComponent(selectedEntity);
    drawLensFlareComponent(selectedEntity);
    drawReflectionProbeComponent(selectedEntity);
    drawIrradianceProbeVolumeComponent(selectedEntity);
    drawTerrainComponent(selectedEntity);
    drawTerrainSculptComponent(selectedEntity);
    drawTerrainFoliageComponent(selectedEntity);
    drawWaterBodyComponent(selectedEntity);
    drawRigidBodyComponent(selectedEntity);
    drawBoxColliderComponent(selectedEntity);
    drawUIElementComponent(selectedEntity);
    drawScriptComponent(selectedEntity);

    ImGui::Spacing();
    drawAddComponentButton(selectedEntity);

    ImGui::End();
}

void PropertiesPanel::drawTagComponent(Entity e) {
    if (!e.hasComponent<TagComponent>()) return;
    auto& tc = e.getComponent<TagComponent>();
    char buf[256];
    copyStringToBuffer(buf, tc.tag);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##tag", buf, sizeof(buf))) {
        tc.tag = buf;
        markEdited();
    }
}

void PropertiesPanel::drawTransformComponent(Entity e) {
    drawComponent<TransformComponent>("Transform", e, [this](TransformComponent& tc) {
        auto vec3Row = [this](const char* label, glm::vec3& v, float reset = 0.0f) {
            ImGui::PushID(label);
            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, 90);
            ImGui::Text("%s", label);
            ImGui::NextColumn();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat3("##v", glm::value_ptr(v), 0.05f))
                markEdited();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                v = glm::vec3(reset);
            ImGui::Columns(1);
            ImGui::PopID();
        };
        vec3Row("Position",  tc.translation, 0.0f);
        vec3Row("Rotation",  tc.rotation,    0.0f);
        vec3Row("Scale",     tc.scale,       1.0f);
    });
}

void PropertiesPanel::drawMeshRendererComponent(Entity e) {
    drawComponent<MeshRendererComponent>("Mesh Renderer", e, [this](MeshRendererComponent& mr) {
        char meshBuf[256];
        copyStringToBuffer(meshBuf, mr.meshPath);
        if (ImGui::InputText("Mesh##path", meshBuf, sizeof(meshBuf), ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit()) { mr.meshPath = meshBuf; markEdited(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##mesh")) { /* file dialog */ }

        char matBuf[256];
        copyStringToBuffer(matBuf, mr.materialPath);
        if (ImGui::InputText("Material", matBuf, sizeof(matBuf), ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit()) { mr.materialPath = matBuf; markEdited(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##mat")) { /* file dialog */ }

        if (ImGui::Checkbox("Cast Shadows",    &mr.castShadows)) markEdited();
        ImGui::SameLine();
        if (ImGui::Checkbox("Receive Shadows", &mr.receiveShadows)) markEdited();
        if (ImGui::Checkbox("Visible",         &mr.visible)) markEdited();
        if (mr.subMeshIndex >= 0) {
            ImGui::TextDisabled("Imported SubMesh: %d", mr.subMeshIndex);
            if (mr.preserveHierarchy)
                ImGui::TextDisabled("Hierarchy Import: On");
        }
    });
}

void PropertiesPanel::drawAnimatorComponent(Entity e) {
    drawComponent<AnimatorComponent>("Animator", e, [this, e](AnimatorComponent& animator) {
        auto findAnimatedMesh = [&](auto&& self, EntityID id) -> std::shared_ptr<Mesh> {
            std::shared_ptr<Mesh> mesh = m_scene ? m_scene->getResolvedMesh(id) : std::shared_ptr<Mesh>{};
            if (mesh && mesh->hasSkeleton() && mesh->hasAnimations())
                return mesh;

            if (!m_scene)
                return {};

            for (EntityID child : m_scene->getChildren(id)) {
                if (std::shared_ptr<Mesh> childMesh = self(self, child))
                    return childMesh;
            }
            return {};
        };

        const std::shared_ptr<Mesh> mesh = findAnimatedMesh(findAnimatedMesh, e.getID());
        if (mesh && mesh->hasAnimations()) {
            if (animator.currentClip.empty())
                animator.currentClip = mesh->getAnimationClips().front().name;

            const char* previewClip = animator.currentClip.empty()
                ? mesh->getAnimationClips().front().name.c_str()
                : animator.currentClip.c_str();

            if (ImGui::BeginCombo("Clip", previewClip)) {
                for (const AnimationClip& clip : mesh->getAnimationClips()) {
                    const bool selected = animator.currentClip == clip.name;
                    if (ImGui::Selectable(clip.name.c_str(), selected)) {
                        if (animator.currentClip.empty() || animator.currentClip == clip.name || animator.blendDuration <= 1e-5f) {
                            animator.currentClip = clip.name;
                            animator.currentTime = 0.0f;
                            animator.nextClip.clear();
                            animator.nextTime = 0.0f;
                            animator.blendElapsed = 0.0f;
                        } else if (animator.nextClip != clip.name) {
                            animator.nextClip = clip.name;
                            animator.nextTime = 0.0f;
                            animator.blendElapsed = 0.0f;
                        }
                        markEdited();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (const AnimationClip* currentClip = mesh->findAnimationClip(animator.currentClip)) {
                float clipTime = std::clamp(animator.currentTime, 0.0f, std::max(currentClip->duration, 0.0f));
                if (ImGui::SliderFloat("Time", &clipTime, 0.0f, std::max(currentClip->duration, 0.001f))) {
                    animator.currentTime = clipTime;
                    animator.nextTime = 0.0f;
                    animator.blendElapsed = 0.0f;
                    animator.nextClip.clear();
                    markEdited();
                }
                ImGui::TextDisabled("Duration: %.2fs", currentClip->duration);
            }

            ImGui::TextDisabled("Clips: %zu  Bones: %zu",
                                mesh->getAnimationClips().size(),
                                mesh->getSkeleton().bones.size());
        } else {
            ImGui::TextDisabled("No animated mesh found on this entity or its children.");
        }

        if (ImGui::Checkbox("Playing", &animator.playing)) markEdited();
        ImGui::SameLine();
        if (ImGui::Checkbox("Looping", &animator.looping)) markEdited();
        if (ImGui::DragFloat("Playback Speed", &animator.playbackSpeed, 0.01f, 0.0f, 4.0f)) markEdited();
        if (ImGui::DragFloat("Blend Duration", &animator.blendDuration, 0.01f, 0.0f, 5.0f)) markEdited();

        if (ImGui::Button("Restart")) {
            animator.currentTime = 0.0f;
            animator.nextTime = 0.0f;
            animator.blendElapsed = 0.0f;
            animator.nextClip.clear();
            markEdited();
        }
        if (!animator.nextClip.empty())
            ImGui::TextDisabled("Queued Clip: %s", animator.nextClip.c_str());
    });
}

void PropertiesPanel::drawMaterialComponent(Entity e) {
    drawComponent<MaterialComponent>("Material", e, [this, e](MaterialComponent& mc) {
        auto markMaterialEdited = [&]() {
            mc.dirty = true;
            markEdited();
        };
        auto isImage = [](const std::filesystem::path& p) {
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
        };

        char matPathBuf[256];
        copyStringToBuffer(matPathBuf, mc.materialPath);
        if (ImGui::InputText("Material File", matPathBuf, sizeof(matPathBuf))) {
            mc.materialPath = matPathBuf;
            if (!mc.materialPath.empty())
                MaterialEditorPanel::loadFromFile(mc.materialPath, mc);
            mc.materialLoaded = !mc.materialPath.empty();
            markMaterialEdited();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* p = static_cast<const char*>(payload->Data);
                if (p) {
                    std::filesystem::path fp(p);
                    auto ext = fp.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".mat" || fp.filename().string().find(".demon.mat") != std::string::npos ||
                        fp.filename().string().find(".demonmat") != std::string::npos) {
                        mc.materialPath = fp.string();
                        MaterialEditorPanel::loadFromFile(mc.materialPath, mc);
                        mc.materialLoaded = true;
                        markMaterialEdited();
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (!mc.materialLoaded && !mc.materialPath.empty()) {
            if (MaterialEditorPanel::loadFromFile(mc.materialPath, mc))
                mc.materialLoaded = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit Material")) {
            if (m_materialEditor) {
                std::filesystem::path path = mc.materialPath;
                if (path.empty()) {
                    std::filesystem::path target = m_assetsRoot.empty() ? std::filesystem::current_path() : m_assetsRoot;
                    std::string name = "Material1.demon.mat";
                    target /= name;
                    mc.materialPath = target.string();
                }
                m_materialEditor->openMaterialForEntity(mc.materialPath, m_scene, e.getID());
            }
        }

        if (ImGui::ColorEdit4("Albedo", glm::value_ptr(mc.albedoColor))) markMaterialEdited();
        if (ImGui::DragFloat("Roughness", &mc.roughness, 0.01f, 0.0f, 1.0f)) markMaterialEdited();
        if (ImGui::DragFloat("Metallic",  &mc.metallic,  0.01f, 0.0f, 1.0f)) markMaterialEdited();

        char albedoBuf[256];
        copyStringToBuffer(albedoBuf, mc.albedoTexture);
        if (ImGui::InputText("Albedo Tex", albedoBuf, sizeof(albedoBuf), ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit()) { mc.albedoTexture = albedoBuf; markMaterialEdited(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##albedo")) { /* file dialog */ }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* p = static_cast<const char*>(payload->Data);
                if (p && isImage(p)) { mc.albedoTexture = p; markMaterialEdited(); }
            }
            ImGui::EndDragDropTarget();
        }

        char normalBuf[256];
        copyStringToBuffer(normalBuf, mc.normalTexture);
        if (ImGui::InputText("Normal Tex", normalBuf, sizeof(normalBuf), ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit()) { mc.normalTexture = normalBuf; markMaterialEdited(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##normal")) { /* file dialog */ }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* p = static_cast<const char*>(payload->Data);
                if (p && isImage(p)) { mc.normalTexture = p; markMaterialEdited(); }
            }
            ImGui::EndDragDropTarget();
        }

        char metallicBuf[256];
        copyStringToBuffer(metallicBuf, mc.metallicTexture);
        if (ImGui::InputText("Metallic Tex", metallicBuf, sizeof(metallicBuf), ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit()) { mc.metallicTexture = metallicBuf; markMaterialEdited(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##metallic")) { /* file dialog */ }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* p = static_cast<const char*>(payload->Data);
                if (p && isImage(p)) { mc.metallicTexture = p; markMaterialEdited(); }
            }
            ImGui::EndDragDropTarget();
        }

        char emissiveBuf[256];
        copyStringToBuffer(emissiveBuf, mc.emissiveTexture);
        if (ImGui::InputText("Emissive Tex", emissiveBuf, sizeof(emissiveBuf), ImGuiInputTextFlags_EnterReturnsTrue)
            || ImGui::IsItemDeactivatedAfterEdit()) { mc.emissiveTexture = emissiveBuf; markMaterialEdited(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##emissive")) { /* file dialog */ }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* p = static_cast<const char*>(payload->Data);
                if (p && isImage(p)) { mc.emissiveTexture = p; markMaterialEdited(); }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::Checkbox("Double Sided", &mc.doubleSided)) markMaterialEdited();
        if (ImGui::Checkbox("Alpha Blend",  &mc.alphaBlend)) markMaterialEdited();
        if (ImGui::DragFloat("Alpha Cutoff", &mc.alphaCutoff, 0.01f, 0.0f, 1.0f)) markMaterialEdited();
    });
}

void PropertiesPanel::drawCameraComponent(Entity e) {
    drawComponent<CameraComponent>("Camera", e, [this](CameraComponent& cc) {
        if (ImGui::Checkbox("Primary", &cc.primary)) markEdited();
        if (ImGui::Checkbox("Fixed Aspect", &cc.fixedAspect)) markEdited();

        const char* types[] = {"Perspective", "Orthographic"};
        int cur = static_cast<int>(cc.camera.getProjectionType());
        if (ImGui::Combo("Projection", &cur, types, 2)) {
            if (cur == 0) cc.camera.setPerspective(60.f, 16.f/9.f, 0.01f, 1000.f);
            else          cc.camera.setOrthographic(10.f, -1.f, 1.f);
            markEdited();
        }
        float fov = cc.camera.getFovY();
        if (ImGui::DragFloat("FOV", &fov, 0.5f, 10.f, 170.f)) {
            cc.camera.setPerspective(fov,
                cc.camera.getAspect(),
                cc.camera.getNearClip(),
                cc.camera.getFarClip());
            markEdited();
        }
        float near_ = cc.camera.getNearClip();
        float far_  = cc.camera.getFarClip();
        if (ImGui::DragFloat("Near Clip", &near_, 0.001f, 0.001f, 10.f)) {
            cc.camera.setPerspective(cc.camera.getFovY(), cc.camera.getAspect(), near_, cc.camera.getFarClip());
            markEdited();
        }
        if (ImGui::DragFloat("Far Clip",  &far_,  1.0f,  10.f, 10000.f)) {
            cc.camera.setPerspective(cc.camera.getFovY(), cc.camera.getAspect(), cc.camera.getNearClip(), far_);
            markEdited();
        }
    });
}

void PropertiesPanel::drawLightComponent(Entity e) {
    drawComponent<LightComponent>("Light", e, [this](LightComponent& lc) {
        const char* types[] = {"Directional", "Point", "Spot"};
        int cur = static_cast<int>(lc.type);
        if (ImGui::Combo("Type", &cur, types, 3)) {
            lc.type = static_cast<LightType>(cur);
            markEdited();
        }
        if (ImGui::ColorEdit3("Color",    glm::value_ptr(lc.color))) markEdited();
        if (ImGui::DragFloat("Intensity", &lc.intensity, 0.1f, 0.0f, 10000.f)) markEdited();
        if (lc.type != LightType::Directional)
            if (ImGui::DragFloat("Range", &lc.range, 0.1f, 0.0f, 500.f)) markEdited();
        if (lc.type == LightType::Spot) {
            if (ImGui::DragFloat("Inner Angle", &lc.innerAngle, 0.5f, 0.f, 90.f)) markEdited();
            if (ImGui::DragFloat("Outer Angle", &lc.outerAngle, 0.5f, 0.f, 90.f)) markEdited();
        }
        if (lc.type != LightType::Directional) {
            char cookiePath[256];
            copyStringToBuffer(cookiePath, lc.cookieTexture);
            const bool entered = ImGui::InputText("Cookie Texture", cookiePath, sizeof(cookiePath),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
            if (entered || deactivated) {
                lc.cookieTexture = cookiePath;
                markEdited();
            }
            if (ImGui::SliderFloat("Cookie Strength", &lc.cookieStrength, 0.0f, 2.0f))
                markEdited();
        }
        if (ImGui::Checkbox("Cast Shadows", &lc.castShadows)) markEdited();
        if (lc.type != LightType::Directional)
            ImGui::TextDisabled("Local point/spot shadow maps: Phase 5 atlas/cubemap pass.");
    });
}

void PropertiesPanel::drawSkyboxComponent(Entity e) {
    drawComponent<SkyboxComponent>("Skybox", e, [this](SkyboxComponent& sc) {
        if (ImGui::Checkbox("Enabled", &sc.enabled)) markEdited();
        char buf[256];
        copyStringToBuffer(buf, sc.texturePath);
        // Only commit on Enter or focus-lost — prevents per-keystroke Texture::loadFromFile spam
        bool entered     = ImGui::InputText("Equirect Tex", buf, sizeof(buf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
        if (entered || deactivated) {
            sc.texturePath = buf;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("…##skytex")) { /* file dialog */ }
        if (ImGui::DragFloat("Intensity", &sc.intensity, 0.01f, 0.0f, 20.0f)) markEdited();
        ImGui::TextDisabled("Use a wide equirectangular image (png/jpg).");
    });
}

void PropertiesPanel::drawFogComponent(Entity e) {
    drawComponent<FogComponent>("Fog", e, [this](FogComponent& fc) {
        if (ImGui::Checkbox("Enabled", &fc.enabled)) markEdited();
        if (ImGui::ColorEdit3("Color", glm::value_ptr(fc.color))) markEdited();
        if (ImGui::DragFloat("Density", &fc.density, 0.001f, 0.0f, 1.0f)) markEdited();
        if (ImGui::DragFloat("Height", &fc.height, 0.01f, -1000.0f, 1000.0f)) markEdited();
        if (ImGui::DragFloat("Height Falloff", &fc.heightFalloff, 0.001f, 0.0f, 5.0f)) markEdited();
        if (ImGui::DragFloat("Start", &fc.start, 0.1f, 0.0f, 10000.0f)) markEdited();
    });
}

void PropertiesPanel::drawVolumetricFogComponent(Entity e) {
    drawComponent<VolumetricFogComponent>("Volumetric Fog", e, [this](VolumetricFogComponent& fog) {
        if (ImGui::Checkbox("Enabled", &fog.enabled)) markEdited();
        if (ImGui::ColorEdit3("Scattering Color", glm::value_ptr(fog.color))) markEdited();
        if (ImGui::SliderFloat("Density", &fog.density, 0.0f, 0.12f, "%.4f")) markEdited();
        if (ImGui::SliderFloat("Intensity", &fog.intensity, 0.0f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Anisotropy", &fog.anisotropy, -0.85f, 0.85f)) markEdited();
        if (ImGui::DragFloat("Height", &fog.height, 0.05f, -1000.0f, 1000.0f)) markEdited();
        if (ImGui::SliderFloat("Height Falloff", &fog.heightFalloff, 0.01f, 1.5f)) markEdited();
        if (ImGui::DragFloat("Start Distance", &fog.startDistance, 0.25f, 0.0f, 500.0f)) markEdited();
        if (ImGui::SliderFloat("Max Opacity", &fog.maxOpacity, 0.0f, 0.95f)) markEdited();
    });
}

void PropertiesPanel::drawLocalVolumetricFogComponent(Entity e) {
    drawComponent<LocalVolumetricFogComponent>("Local Fog Volume", e, [this](LocalVolumetricFogComponent& fog) {
        if (ImGui::Checkbox("Enabled", &fog.enabled)) markEdited();
        if (ImGui::ColorEdit3("Color", glm::value_ptr(fog.color))) markEdited();
        if (ImGui::SliderFloat("Density", &fog.density, 0.0f, 0.18f, "%.4f")) markEdited();
        if (ImGui::SliderFloat("Intensity", &fog.intensity, 0.0f, 2.5f)) markEdited();
        if (ImGui::DragFloat3("Extents", glm::value_ptr(fog.extents), 0.1f, 0.05f, 200.0f)) markEdited();
        if (ImGui::SliderFloat("Soft Edge", &fog.edgeSoftness, 0.05f, 2.0f)) markEdited();
    });
}

void PropertiesPanel::drawVolumetricCloudComponent(Entity e) {
    drawComponent<VolumetricCloudComponent>("Volumetric Clouds", e, [this](VolumetricCloudComponent& clouds) {
        if (ImGui::Checkbox("Enabled", &clouds.enabled)) markEdited();

        const char* presets[] = {"Clear", "Few Clouds", "Cloudy", "Overcast", "Thunder", "Sunset", "Storm"};
        int currentPreset = static_cast<int>(clouds.preset);
        if (ImGui::Combo("Weather Preset", &currentPreset, presets, IM_ARRAYSIZE(presets))) {
            applyCloudPreset(clouds, static_cast<VolumetricCloudPreset>(currentPreset));
            markEdited();
        }

        if (ImGui::SliderFloat("Coverage", &clouds.coverage, 0.0f, 1.0f)) markEdited();
        if (ImGui::SliderFloat("Density", &clouds.density, 0.0f, 1.5f)) markEdited();
        if (ImGui::DragFloat("Altitude", &clouds.altitude, 1.0f, 1.0f, 2000.0f)) markEdited();
        if (ImGui::DragFloat("Thickness", &clouds.thickness, 1.0f, 1.0f, 1000.0f)) markEdited();
        if (ImGui::SliderFloat("Scale", &clouds.scale, 0.05f, 4.0f)) markEdited();
        if (ImGui::SliderFloat("Wind Speed", &clouds.speed, -0.5f, 0.5f, "%.3f")) markEdited();
        if (ImGui::SliderFloat("Darkness", &clouds.darkness, 0.0f, 1.0f)) markEdited();
        if (ImGui::ColorEdit3("Tint", glm::value_ptr(clouds.tint))) markEdited();
        ImGui::TextDisabled("Preset: %s", cloudPresetName(clouds.preset));
    });
}

void PropertiesPanel::drawLensFlareComponent(Entity e) {
    drawComponent<LensFlareComponent>("Lens Flare", e, [this](LensFlareComponent& flare) {
        if (ImGui::Checkbox("Enabled", &flare.enabled)) markEdited();
        if (ImGui::SliderFloat("Intensity", &flare.intensity, 0.0f, 3.0f)) markEdited();
        if (ImGui::SliderFloat("Threshold", &flare.threshold, 0.0f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Halo Width", &flare.haloWidth, 0.01f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Ghost Spacing", &flare.ghostSpacing, 0.05f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Lens Dirt", &flare.dirtIntensity, 0.0f, 1.0f)) markEdited();
        if (ImGui::ColorEdit3("Tint", glm::value_ptr(flare.tint))) markEdited();
    });
}

void PropertiesPanel::drawReflectionProbeComponent(Entity e) {
    drawComponent<ReflectionProbeComponent>("Reflection Probe", e, [this](ReflectionProbeComponent& probe) {
        if (ImGui::Checkbox("Enabled", &probe.enabled)) markEdited();

        char buf[256];
        copyStringToBuffer(buf, probe.assetPath);
        const bool entered = ImGui::InputText("Probe Asset", buf, sizeof(buf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
        if (entered || deactivated) {
            probe.assetPath = buf;
            markEdited();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* path = static_cast<const char*>(payload->Data);
                if (path) {
                    std::filesystem::path droppedPath(path);
                    if (droppedPath.extension() == ".demonprobe") {
                        probe.assetPath = droppedPath.string();
                        markEdited();
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::DragInt("Priority", &probe.priority, 1.0f, -64, 64)) markEdited();
        ImGui::TextDisabled("Center is driven by the entity transform.");
        ImGui::TextDisabled("The .demonprobe asset provides texture, extents, intensity, and blend.");
    });
}

void PropertiesPanel::drawIrradianceProbeVolumeComponent(Entity e) {
    drawComponent<IrradianceProbeVolumeComponent>("Irradiance Probe Volume (DDGI)", e, [this](IrradianceProbeVolumeComponent& volume) {
        if (ImGui::Checkbox("Enabled", &volume.enabled)) markEdited();
        if (ImGui::DragFloat3("Extents", glm::value_ptr(volume.extents), 0.1f, 0.5f, 250.0f)) markEdited();
        if (ImGui::DragFloat3("Probe Counts", glm::value_ptr(volume.probeCounts), 1.0f, 1.0f, 8.0f, "%.0f")) markEdited();
        if (ImGui::ColorEdit3("Tint", glm::value_ptr(volume.tint))) markEdited();
        if (ImGui::SliderFloat("Intensity", &volume.intensity, 0.0f, 3.0f)) markEdited();
        if (ImGui::SliderFloat("Sky Weight", &volume.skyWeight, 0.0f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Bounce Weight", &volume.bounceWeight, 0.0f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Normal Bias", &volume.normalBias, 0.0f, 2.0f)) markEdited();
        if (ImGui::SliderFloat("Leak Reduction", &volume.leakReduction, 0.0f, 1.0f)) markEdited();
        if (ImGui::Checkbox("Dynamic Update", &volume.dynamicUpdate)) markEdited();
        ImGui::TextDisabled("Runtime probe field feeds indirect diffuse; direct lights continue normally.");
    });
}

void PropertiesPanel::drawRigidBodyComponent(Entity e) {
    drawComponent<RigidBodyComponent>("Rigid Body", e, [this](RigidBodyComponent& rb) {
        const char* types[] = {"Static", "Dynamic", "Kinematic"};
        int cur = static_cast<int>(rb.type);
        if (ImGui::Combo("Body Type", &cur, types, 3)) {
            rb.type = static_cast<BodyType>(cur);
            rb.isKinematic = (rb.type == BodyType::Kinematic);
            markEdited();
        }
        if (ImGui::DragFloat("Mass",            &rb.mass,            0.01f, 0.001f, 1000.f)) markEdited();
        if (ImGui::DragFloat("Linear Damping",  &rb.linearDamping,   0.001f, 0.f, 1.f)) markEdited();
        if (ImGui::DragFloat("Angular Damping", &rb.angularDamping,  0.001f, 0.f, 1.f)) markEdited();
        if (ImGui::Checkbox("Use Gravity",      &rb.useGravity)) markEdited();
        if (ImGui::DragFloat("Gravity Scale",   &rb.gravityScale,    0.01f, 0.0f, 8.0f)) markEdited();
        if (ImGui::Checkbox("Simulate Physics", &rb.simulatePhysics)) markEdited();
        if (ImGui::Checkbox("Lock Rotation",    &rb.lockRotation)) markEdited();
        if (ImGui::Checkbox("Continuous Collision", &rb.continuousCollision)) markEdited();
        if (ImGui::Checkbox("Allow Sleeping",   &rb.allowSleeping)) markEdited();
        int layer = static_cast<int>(rb.collisionLayer);
        if (ImGui::InputInt("Collision Layer", &layer)) {
            rb.collisionLayer = static_cast<uint32_t>(std::max(layer, 0));
            markEdited();
        }
        if (ImGui::Checkbox("Lock Position X", &rb.lockPositionX)) markEdited();
        if (ImGui::Checkbox("Lock Position Y", &rb.lockPositionY)) markEdited();
        if (ImGui::Checkbox("Lock Position Z", &rb.lockPositionZ)) markEdited();
        if (ImGui::Checkbox("Lock Rotation X", &rb.lockRotationX)) markEdited();
        if (ImGui::Checkbox("Lock Rotation Y", &rb.lockRotationY)) markEdited();
        if (ImGui::Checkbox("Lock Rotation Z", &rb.lockRotationZ)) markEdited();
        if (ImGui::Checkbox("Is Kinematic",     &rb.isKinematic)) {
            rb.type = rb.isKinematic ? BodyType::Kinematic :
                (rb.type == BodyType::Kinematic ? BodyType::Dynamic : rb.type);
            markEdited();
        }
        if (ImGui::DragFloat3("Linear Velocity", glm::value_ptr(rb.linearVelocity), 0.05f)) markEdited();
        if (ImGui::DragFloat3("Angular Velocity", glm::value_ptr(rb.angularVelocity), 0.05f)) markEdited();
    });
}

void PropertiesPanel::drawBoxColliderComponent(Entity e) {
    drawComponent<BoxColliderComponent>("Box Collider", e, [this](BoxColliderComponent& bc) {
        if (ImGui::DragFloat3("Half Extents", glm::value_ptr(bc.halfExtents), 0.05f, 0.01f, 256.0f)) markEdited();
        if (ImGui::DragFloat3("Offset", glm::value_ptr(bc.offset), 0.05f)) markEdited();
        if (ImGui::DragFloat("Friction", &bc.friction, 0.01f, 0.0f, 2.0f)) markEdited();
        if (ImGui::DragFloat("Restitution", &bc.restitution, 0.01f, 0.0f, 1.0f)) markEdited();
        if (ImGui::Checkbox("Is Trigger", &bc.isTrigger)) markEdited();
    });
}

void PropertiesPanel::drawTerrainComponent(Entity e) {
    drawComponent<TerrainComponent>("Terrain", e, [this, e](TerrainComponent& terrain) mutable {
        ensureTerrainData(terrain);

        int resolution = static_cast<int>(terrain.resolution);
        if (ImGui::DragInt("Resolution", &resolution, 1.0f, 2, 257)) {
            terrain.resolution = sanitizeTerrainResolution(static_cast<uint32_t>(resolution));
            const size_t expectedSize =
                static_cast<size_t>(terrain.resolution) * static_cast<size_t>(terrain.resolution);
            terrain.heights.assign(expectedSize, 0.0f);
            terrain.dirty = true;
            markEdited();
        }
        if (ImGui::DragFloat("Size X", &terrain.sizeX, 0.5f, 1.0f, 4096.0f)) {
            terrain.sizeX = std::max(terrain.sizeX, 1.0f);
            terrain.dirty = true;
            markEdited();
        }
        if (ImGui::DragFloat("Size Z", &terrain.sizeZ, 0.5f, 1.0f, 4096.0f)) {
            terrain.sizeZ = std::max(terrain.sizeZ, 1.0f);
            terrain.dirty = true;
            markEdited();
        }
        if (ImGui::DragFloat("Max Height", &terrain.maxHeight, 0.1f, 0.1f, 1024.0f)) {
            terrain.maxHeight = std::max(terrain.maxHeight, 0.1f);
            terrain.dirty = true;
            markEdited();
        }
        if (ImGui::DragFloat("UV Scale", &terrain.uvScale, 0.1f, 0.1f, 64.0f)) {
            terrain.uvScale = std::max(terrain.uvScale, 0.1f);
            terrain.dirty = true;
            markEdited();
        }
        if (ImGui::ColorEdit4("Low Color", glm::value_ptr(terrain.lowColor))) { terrain.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("Mid Color", glm::value_ptr(terrain.midColor))) { terrain.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("High Color", glm::value_ptr(terrain.highColor))) { terrain.dirty = true; markEdited(); }
        if (ImGui::Checkbox("Cast Shadows", &terrain.castShadows)) { terrain.dirty = true; markEdited(); }
        if (ImGui::Checkbox("Receive Shadows", &terrain.receiveShadows)) { terrain.dirty = true; markEdited(); }
        if (ImGui::Checkbox("Collision Enabled", &terrain.collisionEnabled)) { terrain.dirty = true; markEdited(); }

        if (ImGui::Button("Reset Heights")) {
            terrain.heights.assign(static_cast<size_t>(terrain.resolution) * static_cast<size_t>(terrain.resolution), 0.0f);
            terrain.dirty = true;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Fill Noise")) {
            fillTerrainNoise(terrain);
            if (e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Normalize")) {
            normalizeTerrainHeights(terrain);
            if (e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Invert")) {
            invertTerrainHeights(terrain);
            if (e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }
        if (ImGui::Button("Island Mask")) {
            applyIslandMask(terrain);
            if (e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }

        const float centerHeight = sampleTerrainHeight(terrain, 0.5f, 0.5f);
        ImGui::TextDisabled("Center Height: %.2f", centerHeight);
    });
}

void PropertiesPanel::drawTerrainSculptComponent(Entity e) {
    drawComponent<TerrainSculptComponent>("Terrain Sculpt", e, [this, e](TerrainSculptComponent& sculpt) mutable {
        if (!e.hasComponent<TerrainComponent>()) {
            ImGui::TextDisabled("Terrain Sculpt requires a Terrain component.");
            return;
        }

        auto& terrain = e.getComponent<TerrainComponent>();
        ensureTerrainData(terrain);

        const char* labels[] = {"Raise", "Lower", "Flatten", "Smooth", "Noise", "Terrace", "Erode", "Sharpen"};
        int tool = static_cast<int>(sculpt.tool);
        if (ImGui::Combo("Tool", &tool, labels, 8)) { sculpt.tool = static_cast<TerrainSculptTool>(tool); markEdited(); }
        if (ImGui::DragFloat("Brush Radius", &sculpt.brushRadius, 0.1f, 0.1f, std::max(terrain.sizeX, terrain.sizeZ))) markEdited();
        if (ImGui::DragFloat("Brush Strength", &sculpt.brushStrength, 0.05f, 0.0f, terrain.maxHeight)) markEdited();
        if (ImGui::DragFloat("Brush Falloff", &sculpt.brushFalloff, 0.05f, 0.05f, 6.0f)) markEdited();
        if (ImGui::DragFloat("Flatten Target", &sculpt.flattenTarget, 0.05f, 0.0f, terrain.maxHeight)) markEdited();
        if (sculpt.tool == TerrainSculptTool::Noise && ImGui::DragFloat("Noise Scale", &sculpt.noiseScale, 0.01f, 0.02f, 2.0f)) markEdited();
        if (sculpt.tool == TerrainSculptTool::Terrace && ImGui::DragFloat("Terrace Spacing", &sculpt.terraceSpacing, 0.05f, 0.1f, terrain.maxHeight)) markEdited();
        if (sculpt.tool == TerrainSculptTool::Erode && ImGui::SliderFloat("Erosion Amount", &sculpt.erosionAmount, 0.0f, 1.0f)) markEdited();
        if (sculpt.tool == TerrainSculptTool::Sharpen && ImGui::SliderFloat("Sharpen Amount", &sculpt.sharpenAmount, 0.0f, 1.0f)) markEdited();
        if (ImGui::SliderFloat2("Brush Center", glm::value_ptr(sculpt.brushCenter), 0.0f, 1.0f)) markEdited();
        if (ImGui::Checkbox("Auto Rebuild Details", &sculpt.autoRebuild)) markEdited();

        if (ImGui::Button("Apply Brush")) {
            applyTerrainBrush(terrain, sculpt);
            if (sculpt.autoRebuild && e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Smooth All")) {
            smoothTerrain(terrain);
            if (sculpt.autoRebuild && e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Flatten All")) {
            std::fill(terrain.heights.begin(), terrain.heights.end(), std::clamp(sculpt.flattenTarget, 0.0f, terrain.maxHeight));
            terrain.dirty = true;
            if (sculpt.autoRebuild && e.hasComponent<TerrainFoliageComponent>())
                e.getComponent<TerrainFoliageComponent>().dirty = true;
            markEdited();
        }
        if (e.hasComponent<TerrainFoliageComponent>()) {
            ImGui::SameLine();
            if (ImGui::Button("Rebuild Details")) {
                e.getComponent<TerrainFoliageComponent>().dirty = true;
                markEdited();
            }
        }

        ImGui::TextDisabled("Active Tool: %s", terrainToolLabel(sculpt.tool));
    });
}

void PropertiesPanel::drawTerrainFoliageComponent(Entity e) {
    drawComponent<TerrainFoliageComponent>("Terrain Foliage", e, [this, e](TerrainFoliageComponent& foliage) mutable {
        if (!e.hasComponent<TerrainComponent>()) {
            ImGui::TextDisabled("Terrain Foliage requires a Terrain component.");
            return;
        }

        auto& terrain = e.getComponent<TerrainComponent>();
        ensureTerrainData(terrain);

        int treeCount = static_cast<int>(foliage.treeCount);
        int grassCount = static_cast<int>(foliage.grassCount);
        int randomSeed = static_cast<int>(std::min<uint32_t>(foliage.randomSeed, static_cast<uint32_t>(std::numeric_limits<int>::max())));

        if (ImGui::Checkbox("Trees Enabled", &foliage.treesEnabled)) { foliage.dirty = true; markEdited(); }
        if (ImGui::Checkbox("Grass Enabled", &foliage.grassEnabled)) { foliage.dirty = true; markEdited(); }
        if (drawModelPathInput("Tree Source Mesh", foliage.treeMeshPath)) { foliage.dirty = true; markEdited(); }
        if (drawModelPathInput("Grass Source Mesh", foliage.grassMeshPath)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragInt("Tree Count", &treeCount, 1.0f, 0, 2048)) { foliage.treeCount = static_cast<uint32_t>(std::max(treeCount, 0)); foliage.dirty = true; markEdited(); }
        if (ImGui::DragInt("Grass Count", &grassCount, 4.0f, 0, 16384)) { foliage.grassCount = static_cast<uint32_t>(std::max(grassCount, 0)); foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Tree Min Scale", &foliage.treeMinScale, 0.05f, 0.1f, 8.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Tree Max Scale", &foliage.treeMaxScale, 0.05f, 0.1f, 8.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Grass Min Scale", &foliage.grassMinScale, 0.02f, 0.05f, 4.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Grass Max Scale", &foliage.grassMaxScale, 0.02f, 0.05f, 4.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Placement Jitter", &foliage.placementJitter, 0.01f, 0.0f, 2.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Brush Radius", &foliage.brushRadius, 0.1f, 0.1f, std::max(terrain.sizeX, terrain.sizeZ))) markEdited();
        if (ImGui::SliderFloat("Brush Density", &foliage.brushDensity, 0.01f, 1.0f)) markEdited();
        if (ImGui::SliderFloat2("Brush Center", glm::value_ptr(foliage.brushCenter), 0.0f, 1.0f)) markEdited();
        if (ImGui::SliderFloat("Min Height", &foliage.minHeight, 0.0f, 1.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::SliderFloat("Max Height", &foliage.maxHeight, 0.0f, 1.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Max Slope", &foliage.maxSlopeDegrees, 0.5f, 0.0f, 90.0f)) { foliage.dirty = true; markEdited(); }
        if (ImGui::DragInt("Seed", &randomSeed, 1.0f, 0, std::numeric_limits<int>::max())) { foliage.randomSeed = static_cast<uint32_t>(std::max(randomSeed, 0)); foliage.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("Trunk Color", glm::value_ptr(foliage.treeTrunkColor))) { foliage.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("Leaf Color", glm::value_ptr(foliage.treeLeafColor))) { foliage.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("Grass Color", glm::value_ptr(foliage.grassColor))) { foliage.dirty = true; markEdited(); }
        ImGui::TextDisabled("Painted Trees: %zu  Painted Grass: %zu", foliage.paintedTrees.size(), foliage.paintedGrass.size());

        if (ImGui::Button("Regenerate Foliage")) {
            foliage.dirty = true;
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Paint Trees")) {
            paintFoliageBrush(terrain, foliage, true);
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Paint Grass")) {
            paintFoliageBrush(terrain, foliage, false);
            markEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Brush")) {
            clearFoliageBrush(terrain, foliage);
            markEdited();
        }
    });
}

void PropertiesPanel::drawWaterBodyComponent(Entity e) {
    drawComponent<WaterBodyComponent>("Water Body", e, [this](WaterBodyComponent& water) {
        const char* labels[] = {"Lake", "River", "Ocean", "Pool", "Custom Area"};
        int type = static_cast<int>(water.type);
        if (ImGui::Combo("Type", &type, labels, 5)) { water.type = static_cast<WaterBodyType>(type); water.dirty = true; markEdited(); }

        int resolution = static_cast<int>(water.resolution);
        if (ImGui::DragInt("Resolution", &resolution, 1.0f, 2, 129)) {
            water.resolution = static_cast<uint32_t>(std::clamp(resolution, 2, 129));
            water.dirty = true;
            markEdited();
        }
        if (ImGui::DragFloat2("Size", glm::value_ptr(water.size), 0.25f, 1.0f, 4096.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Depth", &water.depth, 0.1f, 0.1f, 512.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("Shallow Color", glm::value_ptr(water.surfaceColor))) { water.dirty = true; markEdited(); }
        if (ImGui::ColorEdit4("Deep Color", glm::value_ptr(water.bottomColor))) { water.dirty = true; markEdited(); }
        if (ImGui::SliderFloat("Transparency", &water.transparency, 0.0f, 1.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Wave Amplitude", &water.waveAmplitude, 0.01f, 0.0f, 8.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Wave Length", &water.waveLength, 0.1f, 0.1f, 256.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Wave Speed", &water.waveSpeed, 0.01f, 0.0f, 16.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::SliderFloat("Choppiness", &water.choppiness, 0.0f, 2.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::SliderFloat("Surface Roughness", &water.roughness, 0.02f, 1.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::SliderFloat("Foam Intensity", &water.foamIntensity, 0.0f, 2.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::SliderFloat("Edge Fade", &water.edgeFade, 0.0f, 0.45f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat2("Flow Direction", glm::value_ptr(water.flowDirection), 0.01f, -1.0f, 1.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Flow Speed", &water.flowSpeed, 0.05f, 0.0f, 32.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Fluid Density", &water.fluidDensity, 1.0f, 1.0f, 4000.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Drag", &water.drag, 0.01f, 0.0f, 8.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::DragFloat("Buoyancy", &water.buoyancyMultiplier, 0.01f, 0.0f, 8.0f)) { water.dirty = true; markEdited(); }
        if (ImGui::Checkbox("Affects Rigid Bodies", &water.affectsRigidBodies)) { water.dirty = true; markEdited(); }

        ImGui::TextDisabled("Preset: %s", waterTypeLabel(water.type));
        ImGui::TextDisabled("Renderer: v1.9-compatible surface water");
    });
}

void PropertiesPanel::drawScriptComponent(Entity e) {
    drawComponent<ScriptComponent>("Script", e, [this](ScriptComponent& sc) {
        auto& scriptEngine = ScriptEngine::get();
        char buf[256];
        copyStringToBuffer(buf, sc.className);

        if (ImGui::InputText("Behavior", buf, sizeof(buf))) {
            sc.className = buf;
            if (sc.className.empty())
                sc.fieldValues.clear();
            else
                scriptEngine.refreshComponent(sc);
            markEdited();
        }

        const auto behaviorNames = scriptEngine.getBehaviorNames();
        if (!behaviorNames.empty() && ImGui::BeginCombo("Compiled Behaviors", sc.className.empty() ? "<Select Behavior>" : sc.className.c_str())) {
            for (const std::string& behaviorName : behaviorNames) {
                const bool selected = sc.className == behaviorName;
                if (ImGui::Selectable(behaviorName.c_str(), selected)) {
                    sc.className = behaviorName;
                    scriptEngine.refreshComponent(sc);
                    markEdited();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::SmallButton("Compile Scripts")) {
            if (m_scene) {
                if (!scriptEngine.compileAndHotReload(m_scene, nullptr))
                    DEMON_LOG_ERROR("DemonScript: compile failed.");
                else
                    scriptEngine.refreshComponent(sc);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open In IDE"))
            scriptEngine.openBehaviorInIde(sc.className);

        if (const ScriptBehaviorDefinition* behavior = scriptEngine.findBehavior(sc.className)) {
            ImGui::TextDisabled("%s", behavior->path.string().c_str());
            ImGui::TextDisabled("Events: %s%s%s%s",
                                behavior->hasOnSpawn ? "spawn " : "",
                                behavior->hasOnTick ? "tick " : "",
                                behavior->hasOnTrigger ? "trigger " : "",
                                behavior->hasOnSignal ? "signal" : "");

            scriptEngine.refreshComponent(sc);
            auto drawEntityField = [&](ScriptFieldValue& field) {
                auto acceptsEntity = [&](EntityID id) {
                    if (!m_scene)
                        return false;
                    if (field.type == ScriptFieldType::Entity)
                        return true;
                    if (field.type == ScriptFieldType::Entity3D)
                        return m_scene->getComponent<MeshRendererComponent>(id) != nullptr;
                    if (field.type == ScriptFieldType::EntityUI)
                        return m_scene->getComponent<UIElementComponent>(id) != nullptr;
                    if (field.type == ScriptFieldType::EntityImage) {
                        if (const auto* ui = m_scene->getComponent<UIElementComponent>(id)) {
                            return ui->kind == UIElementKind::Image2D ||
                                   ui->kind == UIElementKind::Image3D ||
                                   ui->kind == UIElementKind::Sprite2D;
                        }
                        return false;
                    }
                    return false;
                };

                auto labelForEntity = [&](EntityID id) {
                    if (!m_scene || id == NULL_ENTITY || !m_scene->entityExists(id))
                        return std::string("<None>");
                    const auto* tag = m_scene->getComponent<TagComponent>(id);
                    return tag ? std::format("{} ({})", tag->tag, id) : std::format("Entity {}", id);
                };

                std::string current = labelForEntity(field.entityValue);
                if (ImGui::BeginCombo(field.name.c_str(), current.c_str())) {
                    if (ImGui::Selectable("<None>", field.entityValue == NULL_ENTITY)) {
                        field.entityValue = NULL_ENTITY;
                        markEdited();
                    }
                    if (m_scene) {
                        for (EntityID id : m_scene->getEntities()) {
                            if (!acceptsEntity(id))
                                continue;
                            std::string label = labelForEntity(id);
                            const bool selected = field.entityValue == id;
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                field.entityValue = id;
                                markEdited();
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            };

            for (ScriptFieldValue& field : sc.fieldValues) {
                if (field.hidden)
                    continue;

                switch (field.type) {
                    case ScriptFieldType::Bool:
                        if (ImGui::Checkbox(field.name.c_str(), &field.boolValue))
                            markEdited();
                        break;
                    case ScriptFieldType::Int:
                        if (ImGui::DragScalar(field.name.c_str(), ImGuiDataType_S64, &field.intValue, 1.0f))
                            markEdited();
                        break;
                    case ScriptFieldType::Float:
                        if (ImGui::DragFloat(field.name.c_str(), &field.floatValue, 0.05f))
                            markEdited();
                        break;
                    case ScriptFieldType::String: {
                        char stringBuf[256];
                        copyStringToBuffer(stringBuf, field.stringValue);
                        if (ImGui::InputText(field.name.c_str(), stringBuf, sizeof(stringBuf))) {
                            field.stringValue = stringBuf;
                            markEdited();
                        }
                        break;
                    }
                    case ScriptFieldType::Vec3:
                        if (ImGui::DragFloat3(field.name.c_str(), glm::value_ptr(field.vec3Value), 0.05f))
                            markEdited();
                        break;
                    case ScriptFieldType::Entity:
                    case ScriptFieldType::Entity3D:
                    case ScriptFieldType::EntityImage:
                    case ScriptFieldType::EntityUI:
                        drawEntityField(field);
                        break;
                    default:
                        break;
                }
            }
        } else if (!sc.className.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                               "Behavior '%s' is not compiled yet.",
                               sc.className.c_str());
        } else {
            ImGui::TextDisabled("Assign a compiled DemonScript behavior to expose properties.");
        }
    });
}

void PropertiesPanel::drawUIElementComponent(Entity e) {
    const EntityID entityId = e.getID();
    drawComponent<UIElementComponent>("UI Element", e, [this, entityId](UIElementComponent& ui) {
        auto syncRenderProxy = [&]() {
            if (!m_scene)
                return;
            if (MeshRendererComponent* mesh = m_scene->getComponent<MeshRendererComponent>(entityId))
                mesh->visible = ui.visible;
            if (MaterialComponent* material = m_scene->getComponent<MaterialComponent>(entityId)) {
                material->albedoColor = ui.color;
                material->albedoTexture = ui.imagePath;
                material->doubleSided = true;
                material->alphaBlend = true;
                material->dirty = true;
            }
        };

        const char* kinds[] = {
            "UI / Text",
            "UI / Image",
            "UI / Shape",
            "UI / Sprite",
            "3D UI / Text",
            "3D UI / Image",
        };
        int kind = static_cast<int>(ui.kind);
        if (ImGui::Combo("Kind", &kind, kinds, IM_ARRAYSIZE(kinds))) {
            ui.kind = static_cast<UIElementKind>(std::clamp(kind, 0, static_cast<int>(IM_ARRAYSIZE(kinds) - 1)));
            ui.screenSpace = ui.kind == UIElementKind::Text2D ||
                             ui.kind == UIElementKind::Image2D ||
                             ui.kind == UIElementKind::Shape2D ||
                             ui.kind == UIElementKind::Sprite2D;
            markEdited();
        }

        if (ImGui::Checkbox("Visible", &ui.visible)) {
            syncRenderProxy();
            markEdited();
        }
        if (ImGui::Checkbox("Screen Space", &ui.screenSpace))
            markEdited();
        if (ImGui::Checkbox("Billboard", &ui.billboard))
            markEdited();

        char textBuf[512];
        copyStringToBuffer(textBuf, ui.text);
        if (ImGui::InputText("Text", textBuf, sizeof(textBuf))) {
            ui.text = textBuf;
            markEdited();
        }

        char imageBuf[512];
        copyStringToBuffer(imageBuf, ui.imagePath);
        if (ImGui::InputText("Image Path", imageBuf, sizeof(imageBuf))) {
            ui.imagePath = imageBuf;
            syncRenderProxy();
            markEdited();
        }

        if (ImGui::ColorEdit4("Color", glm::value_ptr(ui.color))) {
            syncRenderProxy();
            markEdited();
        }
        if (ImGui::DragFloat2("Size", glm::value_ptr(ui.size), 1.0f, 1.0f, 4096.0f))
            markEdited();
        if (ImGui::DragFloat("Font Size", &ui.fontSize, 1.0f, 6.0f, 256.0f))
            markEdited();
        if (ImGui::DragFloat("Depth", &ui.depth, 0.01f, -1000.0f, 1000.0f))
            markEdited();

        const char* shapes[] = {"Rectangle", "Circle", "Rounded Rectangle"};
        int shape = static_cast<int>(ui.shape);
        if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
            ui.shape = static_cast<UIShapeKind>(std::clamp(shape, 0, static_cast<int>(IM_ARRAYSIZE(shapes) - 1)));
            markEdited();
        }
    });
}

void PropertiesPanel::drawAddComponentButton(Entity e) {
    float w = 180.0f;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - w) * 0.5f);
    if (ImGui::Button("+ Add Component", {w, 0}))
        ImGui::OpenPopup("AddComponent");

    if (ImGui::BeginPopup("AddComponent")) {
        auto addIf = [&]<typename T>(const char* label) {
            if (!e.hasComponent<T>() && ImGui::MenuItem(label)) {
                e.addComponent<T>();
                ImGui::CloseCurrentPopup();
                markEdited();
            }
        };
        addIf.operator()<MeshRendererComponent>("Mesh Renderer");
        addIf.operator()<AnimatorComponent>    ("Animator");
        addIf.operator()<MaterialComponent>    ("Material");
        addIf.operator()<CameraComponent>      ("Camera");
        addIf.operator()<LightComponent>       ("Light");
        addIf.operator()<SkyboxComponent>      ("Skybox");
        addIf.operator()<FogComponent>         ("Fog");
        addIf.operator()<VolumetricFogComponent>("Volumetric Fog");
        addIf.operator()<LocalVolumetricFogComponent>("Local Fog Volume");
        addIf.operator()<VolumetricCloudComponent>("Volumetric Clouds");
        addIf.operator()<LensFlareComponent>   ("Lens Flare");
        addIf.operator()<IrradianceProbeVolumeComponent>("Irradiance Probe Volume");
        if (!e.hasComponent<ReflectionProbeComponent>() && ImGui::MenuItem("Reflection Probe")) {
            if (!e.hasComponent<TransformComponent>())
                e.addComponent<TransformComponent>();
            e.addComponent<ReflectionProbeComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (!e.hasComponent<TerrainComponent>() && ImGui::MenuItem("Terrain")) {
            if (!e.hasComponent<TransformComponent>())
                e.addComponent<TransformComponent>();
            e.addComponent<TerrainComponent>();
            if (!e.hasComponent<TerrainSculptComponent>())
                e.addComponent<TerrainSculptComponent>();
            if (!e.hasComponent<TerrainFoliageComponent>())
                e.addComponent<TerrainFoliageComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (e.hasComponent<TerrainComponent>() && !e.hasComponent<TerrainSculptComponent>() && ImGui::MenuItem("Terrain Sculpt")) {
            e.addComponent<TerrainSculptComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (e.hasComponent<TerrainComponent>() && !e.hasComponent<TerrainFoliageComponent>() && ImGui::MenuItem("Terrain Foliage")) {
            e.addComponent<TerrainFoliageComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (!e.hasComponent<WaterBodyComponent>() && ImGui::MenuItem("Water Body")) {
            if (!e.hasComponent<TransformComponent>())
                e.addComponent<TransformComponent>();
            e.addComponent<WaterBodyComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (!e.hasComponent<RigidBodyComponent>() && ImGui::MenuItem("Rigid Body")) {
            if (!e.hasComponent<TransformComponent>())
                e.addComponent<TransformComponent>();
            e.addComponent<RigidBodyComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (!e.hasComponent<BoxColliderComponent>() && ImGui::MenuItem("Box Collider")) {
            if (!e.hasComponent<TransformComponent>())
                e.addComponent<TransformComponent>();
            e.addComponent<BoxColliderComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        if (!e.hasComponent<UIElementComponent>() && ImGui::MenuItem("UI Element")) {
            if (!e.hasComponent<TransformComponent>())
                e.addComponent<TransformComponent>();
            e.addComponent<UIElementComponent>();
            ImGui::CloseCurrentPopup();
            markEdited();
        }
        addIf.operator()<ScriptComponent>      ("Script");
        addIf.operator()<AudioSourceComponent> ("Audio Source");
        ImGui::EndPopup();
    }
}

} // namespace Demon
