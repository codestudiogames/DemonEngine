#include "EnvironmentPanel.h"

#include <imgui.h>

#include "../EditorSettings.h"

namespace Demon {

namespace {

bool DragColor3(const char* label, glm::vec3& color)
{
    return ImGui::ColorEdit3(label, glm::value_ptr(color));
}

void mark(bool changed, bool& edited)
{
    if (changed)
        edited = true;
}

void applyCloudPresetValues(VolumetricCloudComponent& clouds, VolumetricCloudPreset preset)
{
    clouds.preset = preset;
    clouds.enabled = preset != VolumetricCloudPreset::Clear;
    switch (preset) {
        case VolumetricCloudPreset::Clear:
            clouds.coverage = 0.05f; clouds.density = 0.08f; clouds.darkness = 0.02f; clouds.tint = {1.0f, 1.0f, 1.0f};
            break;
        case VolumetricCloudPreset::FewClouds:
            clouds.coverage = 0.28f; clouds.density = 0.42f; clouds.darkness = 0.08f; clouds.tint = {1.0f, 0.99f, 0.95f};
            break;
        case VolumetricCloudPreset::Cloudy:
            clouds.coverage = 0.55f; clouds.density = 0.68f; clouds.darkness = 0.22f; clouds.tint = {1.0f, 0.98f, 0.92f};
            break;
        case VolumetricCloudPreset::Overcast:
            clouds.coverage = 0.82f; clouds.density = 0.95f; clouds.darkness = 0.44f; clouds.tint = {0.78f, 0.83f, 0.88f};
            break;
        case VolumetricCloudPreset::Thunder:
            clouds.coverage = 0.88f; clouds.density = 1.12f; clouds.darkness = 0.78f; clouds.tint = {0.25f, 0.27f, 0.31f};
            break;
        case VolumetricCloudPreset::Sunset:
            clouds.coverage = 0.46f; clouds.density = 0.62f; clouds.darkness = 0.18f; clouds.tint = {1.0f, 0.70f, 0.46f};
            break;
        case VolumetricCloudPreset::Storm:
            clouds.coverage = 0.92f; clouds.density = 1.25f; clouds.darkness = 0.86f; clouds.tint = {0.20f, 0.23f, 0.28f};
            break;
    }
}

} // namespace

void EnvironmentPanel::applyWorldPreset(RendererSettings& settings, int preset)
{
    switch (preset) {
        case 0: // Exterior Day
            settings.atmosphere.enabled = true;
            settings.atmosphere.density = 0.92f;
            settings.atmosphere.skyBlend = 0.72f;
            settings.atmosphere.sunDiskIntensity = 18.0f;
            settings.ibl.diffuseIntensity = 1.0f;
            settings.ibl.specularIntensity = 1.25f;
            settings.gi.enabled = false;
            break;
        case 1: // Horror Interior
            settings.atmosphere.enabled = false;
            settings.atmosphere.skyBlend = 0.0f;
            settings.ibl.diffuseIntensity = 0.12f;
            settings.ibl.specularIntensity = 0.35f;
            settings.gi.enabled = true;
            settings.gi.diffuseBounce = 0.18f;
            settings.gi.specularBounce = 0.08f;
            settings.gi.tint = {0.55f, 0.62f, 0.78f};
            break;
        case 2: // Night Exterior
            settings.atmosphere.enabled = true;
            settings.atmosphere.density = 0.62f;
            settings.atmosphere.skyBlend = 0.38f;
            settings.atmosphere.sunDiskIntensity = 3.0f;
            settings.ibl.diffuseIntensity = 0.22f;
            settings.ibl.specularIntensity = 0.55f;
            settings.gi.enabled = true;
            settings.gi.diffuseBounce = 0.25f;
            settings.gi.specularBounce = 0.10f;
            settings.gi.tint = {0.42f, 0.50f, 0.72f};
            break;
        case 3: // Overcast
            settings.atmosphere.enabled = true;
            settings.atmosphere.density = 1.35f;
            settings.atmosphere.skyBlend = 0.85f;
            settings.atmosphere.sunDiskIntensity = 6.0f;
            settings.ibl.diffuseIntensity = 0.82f;
            settings.ibl.specularIntensity = 0.82f;
            settings.gi.enabled = true;
            settings.gi.diffuseBounce = 0.32f;
            settings.gi.specularBounce = 0.12f;
            settings.gi.tint = {0.86f, 0.90f, 0.96f};
            break;
        default:
            break;
    }
}

void EnvironmentPanel::render(Renderer& renderer, const std::shared_ptr<Scene>& scene)
{
    ImGui::Begin("Environment", nullptr, editorPanelFlags());

    RendererSettings& settings = renderer.settings();

    if (ImGui::CollapsingHeader("Rendering / Shaders", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Reload Shaders (Hot)"))
            m_shaderReloadRequested = true;
        ImGui::SameLine();
        ImGui::TextDisabled("Recompiles HLSL and hot-swaps GPU pipelines (Tools > Hot Reload Shaders / Ctrl+R).");
    }

    if (ImGui::CollapsingHeader("World Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Exterior Day")) applyWorldPreset(settings, 0);
        ImGui::SameLine();
        if (ImGui::Button("Horror Interior")) applyWorldPreset(settings, 1);
        if (ImGui::Button("Night Exterior")) applyWorldPreset(settings, 2);
        ImGui::SameLine();
        if (ImGui::Button("Overcast")) applyWorldPreset(settings, 3);
    }

    if (ImGui::CollapsingHeader("World Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Analytic Sky", &settings.atmosphere.enabled);
        if (settings.atmosphere.enabled) {
            ImGui::SliderFloat("Sky Density", &settings.atmosphere.density, 0.0f, 3.0f);
            ImGui::SliderFloat("Height Falloff", &settings.atmosphere.heightFalloff, 0.02f, 1.0f);
            ImGui::SliderFloat("Scattering", &settings.atmosphere.anisotropy, -0.5f, 0.85f);
            ImGui::SliderFloat("Sun Disk Size", &settings.atmosphere.sunDiskSize, 0.005f, 0.12f);
            ImGui::SliderFloat("Sun Disk Intensity", &settings.atmosphere.sunDiskIntensity, 0.0f, 40.0f);
            ImGui::SliderFloat("Sky Blend", &settings.atmosphere.skyBlend, 0.0f, 1.0f);
            ImGui::SliderFloat("Aerial Perspective", &settings.atmosphere.aerialPerspective, 0.0f, 1.0f);
        }
        ImGui::SeparatorText("Sky Light / IBL");
        ImGui::SliderFloat("Diffuse", &settings.ibl.diffuseIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Specular", &settings.ibl.specularIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Reflection Boost", &settings.ibl.reflectionBoost, 0.0f, 3.0f);
        ImGui::SliderFloat("Probe Blend", &settings.ibl.localProbeBlend, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Height Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
        FogComponent* fog = firstComponent<FogComponent>(scene);
        if (!fog) {
            if (ImGui::Button("Create Height Fog")) {
                fog = &ensureWorldComponent<FogComponent>(scene, "World Height Fog");
                fog->enabled = true;
                fog->density = 0.018f;
            }
        }
        if (fog) {
            mark(ImGui::Checkbox("Enabled##heightfog", &fog->enabled), m_sceneEdited);
            mark(DragColor3("Color##heightfog", fog->color), m_sceneEdited);
            mark(ImGui::SliderFloat("Density##heightfog", &fog->density, 0.0f, 0.12f, "%.4f"), m_sceneEdited);
            mark(ImGui::DragFloat("Height##heightfog", &fog->height, 0.05f, -500.0f, 500.0f), m_sceneEdited);
            mark(ImGui::SliderFloat("Falloff##heightfog", &fog->heightFalloff, 0.01f, 1.5f), m_sceneEdited);
            mark(ImGui::DragFloat("Start Distance##heightfog", &fog->start, 0.25f, 0.0f, 1000.0f), m_sceneEdited);
        }
    }

    if (ImGui::CollapsingHeader("Volumetrics", ImGuiTreeNodeFlags_DefaultOpen)) {
        VolumetricFogComponent* fog = firstComponent<VolumetricFogComponent>(scene);
        if (!fog) {
            if (ImGui::Button("Create Volumetric Fog")) {
                fog = &ensureWorldComponent<VolumetricFogComponent>(scene, "World Volumetric Fog");
                fog->enabled = true;
                fog->density = 0.008f;
                fog->intensity = 0.34f;
                fog->maxOpacity = 0.28f;
            }
        }
        if (fog) {
            mark(ImGui::Checkbox("Fog Enabled", &fog->enabled), m_sceneEdited);
            mark(DragColor3("Scattering Color", fog->color), m_sceneEdited);
            mark(ImGui::SliderFloat("Fog Density", &fog->density, 0.0f, 0.08f, "%.4f"), m_sceneEdited);
            mark(ImGui::SliderFloat("Fog Intensity", &fog->intensity, 0.0f, 1.5f), m_sceneEdited);
            mark(ImGui::SliderFloat("Max Opacity", &fog->maxOpacity, 0.0f, 0.75f), m_sceneEdited);
            mark(ImGui::SliderFloat("Anisotropy", &fog->anisotropy, -0.85f, 0.85f), m_sceneEdited);
        }

        LocalVolumetricFogComponent* localFog = firstComponent<LocalVolumetricFogComponent>(scene);
        if (!localFog) {
            if (ImGui::Button("Create Local Fog Volume"))
                localFog = &ensureWorldComponent<LocalVolumetricFogComponent>(scene, "Local Fog Volume");
        }
        if (localFog) {
            mark(ImGui::Checkbox("Local Volume Enabled", &localFog->enabled), m_sceneEdited);
            mark(DragColor3("Local Color", localFog->color), m_sceneEdited);
            mark(ImGui::SliderFloat("Local Density", &localFog->density, 0.0f, 0.18f, "%.4f"), m_sceneEdited);
            mark(ImGui::SliderFloat("Local Intensity", &localFog->intensity, 0.0f, 2.5f), m_sceneEdited);
            mark(ImGui::DragFloat3("Local Extents", glm::value_ptr(localFog->extents), 0.1f, 0.05f, 200.0f), m_sceneEdited);
            mark(ImGui::SliderFloat("Local Soft Edge", &localFog->edgeSoftness, 0.05f, 2.0f), m_sceneEdited);
        }

        VolumetricCloudComponent* clouds = firstComponent<VolumetricCloudComponent>(scene);
        if (!clouds) {
            if (ImGui::Button("Create Volumetric Clouds"))
                clouds = &ensureWorldComponent<VolumetricCloudComponent>(scene, "World Volumetric Clouds");
        }
        if (clouds) {
            const char* presets[] = {"Clear", "Few Clouds", "Cloudy", "Overcast", "Thunder", "Sunset", "Storm"};
            int preset = static_cast<int>(clouds->preset);
            mark(ImGui::Checkbox("Clouds Enabled", &clouds->enabled), m_sceneEdited);
            if (ImGui::Combo("Cloud Preset", &preset, presets, IM_ARRAYSIZE(presets))) {
                applyCloudPresetValues(*clouds, static_cast<VolumetricCloudPreset>(preset));
                m_sceneEdited = true;
            }
            mark(ImGui::SliderFloat("Coverage", &clouds->coverage, 0.0f, 1.0f), m_sceneEdited);
            mark(ImGui::SliderFloat("Cloud Density", &clouds->density, 0.0f, 1.5f), m_sceneEdited);
            mark(ImGui::SliderFloat("Darkness", &clouds->darkness, 0.0f, 1.0f), m_sceneEdited);
            mark(DragColor3("Cloud Tint", clouds->tint), m_sceneEdited);
        }
    }

    if (ImGui::CollapsingHeader("Global Illumination", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable GI", &settings.gi.enabled);
        const char* giModes[] = {"Realtime", "Baked"};
        int giMode = static_cast<int>(settings.gi.mode);
        if (ImGui::Combo("GI Mode", &giMode, giModes, IM_ARRAYSIZE(giModes)))
            settings.gi.mode = static_cast<GlobalIlluminationSettings::Mode>(giMode);
        ImGui::SliderFloat("Diffuse Bounce", &settings.gi.diffuseBounce, 0.0f, 1.5f);
        ImGui::SliderFloat("Specular Bounce", &settings.gi.specularBounce, 0.0f, 1.0f);
        ImGui::SliderFloat("Influence Radius", &settings.gi.radius, 1.0f, 120.0f);
        ImGui::SliderFloat("Color Bleed", &settings.gi.colorBleed, 0.0f, 1.0f);
        DragColor3("GI Tint", settings.gi.tint);
        if (ImGui::Button("Apply GI")) {
            settings.gi.enabled = true;
            m_giApplyRequested = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(settings.gi.mode == GlobalIlluminationSettings::Mode::Realtime
            ? "Realtime compiles live DDGI shaders."
            : "Baked compiles shaders and writes bake metadata.");

        IrradianceProbeVolumeComponent* volume = firstComponent<IrradianceProbeVolumeComponent>(scene);
        if (!volume) {
            if (ImGui::Button("Create DDGI Probe Volume"))
                volume = &ensureWorldComponent<IrradianceProbeVolumeComponent>(scene, "Irradiance Probe Volume");
        }
        if (volume) {
            mark(ImGui::Checkbox("DDGI Volume Enabled", &volume->enabled), m_sceneEdited);
            mark(ImGui::DragFloat3("DDGI Extents", glm::value_ptr(volume->extents), 0.1f, 0.5f, 250.0f), m_sceneEdited);
            mark(ImGui::DragFloat3("DDGI Counts", glm::value_ptr(volume->probeCounts), 1.0f, 1.0f, 8.0f, "%.0f"), m_sceneEdited);
            mark(ImGui::SliderFloat("DDGI Intensity", &volume->intensity, 0.0f, 3.0f), m_sceneEdited);
            mark(ImGui::SliderFloat("DDGI Leak Reduction", &volume->leakReduction, 0.0f, 1.0f), m_sceneEdited);
        }
    }

    if (ImGui::CollapsingHeader("Lens / Probe")) {
        LensFlareComponent* flare = firstComponent<LensFlareComponent>(scene);
        if (!flare) {
            if (ImGui::Button("Create Lens Flare"))
                flare = &ensureWorldComponent<LensFlareComponent>(scene, "World Lens Flare");
        }
        if (flare) {
            mark(ImGui::Checkbox("Lens Flare Enabled", &flare->enabled), m_sceneEdited);
            mark(ImGui::SliderFloat("Flare Intensity", &flare->intensity, 0.0f, 3.0f), m_sceneEdited);
            mark(ImGui::SliderFloat("Threshold", &flare->threshold, 0.0f, 2.0f), m_sceneEdited);
            mark(DragColor3("Flare Tint", flare->tint), m_sceneEdited);
        }
    }

    if (ImGui::CollapsingHeader("Shadows")) {
        ImGui::Checkbox("Cascaded Shadows", &settings.shadows.enabled);
        ImGui::SliderFloat("Distance", &settings.shadows.maxDistance, 20.0f, 300.0f);
        ImGui::SliderFloat("Strength", &settings.shadows.strength, 0.0f, 1.0f);
        ImGui::SliderFloat("Bias", &settings.shadows.bias, 0.0f, 0.30f, "%.3f m");
        ImGui::SliderFloat("Normal Bias", &settings.shadows.normalBias, 0.0f, 6.0f, "%.2f texels");
        ImGui::SliderFloat("Softness", &settings.shadows.softness, 0.5f, 3.0f);
    }

    ImGui::End();
}

} // namespace Demon
