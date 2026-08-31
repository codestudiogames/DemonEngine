#include <imgui.h>
#include "StatsPanel.h"
#include "core/Application.h"
#include "../EditorSettings.h"
namespace Demon {
void StatsPanel::render(const RenderStats& stats) {
    ImGui::Begin("Stats", nullptr, editorPanelFlags());
    float dt = ImGui::GetIO().DeltaTime;
    m_frameTimes[m_frameIdx++ % HISTORY] = dt * 1000.f;
    m_fpsSmoothed = 0.9f * m_fpsSmoothed + 0.1f * (dt > 0 ? 1.f / dt : 0.f);
    ImGui::Text("FPS:        %.1f", m_fpsSmoothed);
    ImGui::Text("Frame Time: %.2f ms", dt * 1000.f);
    ImGui::Separator();
    ImGui::Text("Draw Calls: %u", stats.drawCalls);
    ImGui::Text("Vertices:   %u", stats.vertexCount);
    ImGui::Text("Indices:    %u", stats.indexCount);
    ImGui::Text("GBuffer Draws: %u", stats.gbufferDrawCalls);
    ImGui::Text("Compute Dispatches: %u", stats.computeDispatches);
    ImGui::Text("Compute Tiles: %u", stats.computeTiles);
    ImGui::Text("GPU Time:   %.2f ms", stats.gpuTimeMs);
    ImGui::Separator();
    ImGui::PlotLines("##ft", m_frameTimes, HISTORY, m_frameIdx % HISTORY,
                     nullptr, 0.f, 33.f, {0, 60});
    ImGui::TextDisabled("Frame time (0-33 ms)");

    if (ImGui::CollapsingHeader("Post FX", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& renderer = Application::get().getRenderer();
        auto& pp = renderer.getPostProcessing();
        bool enabled = pp.isEnabled();
        if (ImGui::Checkbox("Enabled", &enabled))
            pp.setEnabled(enabled);

        auto& s = pp.settings();
        ImGui::Checkbox("TAA", &s.taa.enabled);
        if (s.taa.enabled) {
            ImGui::SliderFloat("TAA Feedback", &s.taa.feedback, 0.0f, 0.98f);
            ImGui::SliderFloat("TAA Sharpness", &s.taa.sharpness, 0.0f, 0.6f);
            ImGui::SliderFloat("TAA Jitter", &s.taa.jitterScale, 0.25f, 2.0f);
            ImGui::SliderFloat("TAA Motion Reject", &s.taa.motionRejection, 0.0f, 1.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Volumetric Fog", &s.volumetric.enabled);
        if (s.volumetric.enabled) {
            ImGui::ColorEdit3("Vol Fog Color", glm::value_ptr(s.volumetric.color));
            ImGui::SliderFloat("Volumetric Density", &s.volumetric.density, 0.0f, 0.12f, "%.4f");
            ImGui::SliderFloat("Volumetric Intensity", &s.volumetric.intensity, 0.0f, 2.0f);
            ImGui::SliderFloat("Volumetric Anisotropy", &s.volumetric.anisotropy, -0.85f, 0.85f);
            ImGui::SliderFloat("Vol Start Distance", &s.volumetric.startDistance, 0.0f, 30.0f);
            ImGui::SliderFloat("Vol Max Opacity", &s.volumetric.maxOpacity, 0.0f, 0.95f);
        }

        ImGui::Checkbox("Volumetric Clouds", &s.clouds.enabled);
        if (s.clouds.enabled) {
            ImGui::SliderFloat("Cloud Coverage", &s.clouds.coverage, 0.0f, 1.0f);
            ImGui::SliderFloat("Cloud Density", &s.clouds.density, 0.0f, 1.5f);
            ImGui::DragFloat("Cloud Altitude", &s.clouds.altitude, 1.0f, 1.0f, 2000.0f);
            ImGui::DragFloat("Cloud Thickness", &s.clouds.thickness, 1.0f, 1.0f, 1000.0f);
            ImGui::SliderFloat("Cloud Darkness", &s.clouds.darkness, 0.0f, 1.0f);
        }

        ImGui::Checkbox("Lens Flare", &s.lensFlare.enabled);
        if (s.lensFlare.enabled) {
            ImGui::SliderFloat("Flare Intensity", &s.lensFlare.intensity, 0.0f, 3.0f);
            ImGui::SliderFloat("Flare Threshold", &s.lensFlare.threshold, 0.0f, 2.0f);
            ImGui::SliderFloat("Flare Halo", &s.lensFlare.haloWidth, 0.01f, 2.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Motion Blur", &s.motionBlur.enabled);
        if (s.motionBlur.enabled) {
            ImGui::SliderFloat("Shutter Scale", &s.motionBlur.shutterScale, 0.0f, 2.0f);
            ImGui::SliderFloat("Max Blur Pixels", &s.motionBlur.maxBlurPixels, 0.0f, 32.0f);
            ImGui::SliderFloat("Depth Threshold", &s.motionBlur.depthThreshold, 0.0005f, 0.02f, "%.4f");
            ImGui::SliderInt("Motion Samples", &s.motionBlur.sampleCount, 4, 16);
        }

        ImGui::Separator();
        ImGui::Checkbox("Bloom", &s.bloom.enabled);
        if (s.bloom.enabled) {
            ImGui::SliderFloat("Bloom Threshold", &s.bloom.threshold, 0.0f, 4.0f);
            ImGui::SliderFloat("Bloom Intensity", &s.bloom.intensity, 0.0f, 2.0f);
            ImGui::SliderFloat("Bloom Radius", &s.bloom.radius, 0.5f, 4.0f);
            ImGui::SliderFloat("Bloom Soft Knee", &s.bloom.softKnee, 0.0f, 1.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Filmic Tonemap", &s.aces.enabled);
        if (s.aces.enabled) {
            int tonemapMode = (s.aces.mode == TonemapOperator::Reinhard) ? 1 : 0;
            const char* tonemapModes[] = { "ACES Filmic", "Reinhard" };
            if (ImGui::Combo("Tonemap Operator", &tonemapMode, tonemapModes, IM_ARRAYSIZE(tonemapModes)))
                s.aces.mode = (tonemapMode == 1) ? TonemapOperator::Reinhard : TonemapOperator::ACESFilm;
            ImGui::SliderFloat("Exposure", &s.aces.exposure, 0.1f, 4.0f);
            ImGui::SliderFloat("Contrast", &s.aces.contrast, 0.75f, 1.5f);
            ImGui::SliderFloat("Saturation", &s.aces.saturation, 0.0f, 1.5f);
            ImGui::SliderFloat("Vignette", &s.aces.vignette, 0.0f, 0.6f);
            ImGui::SliderFloat("Film Grain", &s.aces.grain, 0.0f, 0.12f);
            ImGui::SliderFloat("Temperature", &s.aces.temperature, -1.0f, 1.0f);
            ImGui::SliderFloat("Tint", &s.aces.tint, -1.0f, 1.0f);
            ImGui::SliderFloat("Lift", &s.aces.lift, -0.3f, 0.3f);
            ImGui::SliderFloat("Gamma", &s.aces.gamma, 0.6f, 1.8f);
            ImGui::SliderFloat("Gain", &s.aces.gain, 0.6f, 1.8f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Screen-Space GI", &s.ssgi.enabled);
        if (s.ssgi.enabled) {
            ImGui::SliderFloat("SSGI Radius", &s.ssgi.radius, 0.25f, 8.0f);
            ImGui::SliderFloat("SSGI Intensity", &s.ssgi.intensity, 0.0f, 3.0f);
            ImGui::SliderFloat("SSGI Thickness", &s.ssgi.thickness, 0.02f, 1.5f);
            ImGui::SliderFloat("SSGI Saturation", &s.ssgi.saturation, 0.0f, 2.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("SSAO", &s.ssao.enabled);
        if (s.ssao.enabled) {
            ImGui::SliderFloat("SSAO Radius", &s.ssao.radius, 0.1f, 3.0f);
            ImGui::SliderFloat("SSAO Power", &s.ssao.power, 0.1f, 4.0f);
            ImGui::SliderFloat("SSAO Bias", &s.ssao.bias, 0.0f, 0.1f);
        }

        ImGui::Separator();
        auto& rs = renderer.settings();
        ImGui::TextDisabled("Lighting / Atmosphere");
        ImGui::Checkbox("Cascaded Shadows", &rs.shadows.enabled);
        if (rs.shadows.enabled) {
            ImGui::SliderFloat("Shadow Distance", &rs.shadows.maxDistance, 20.0f, 300.0f);
            ImGui::SliderFloat("Shadow Strength", &rs.shadows.strength, 0.0f, 1.0f);
            ImGui::SliderFloat("Shadow Bias", &rs.shadows.bias, 0.0f, 0.30f, "%.3f m");
            ImGui::SliderFloat("Shadow Normal Bias", &rs.shadows.normalBias, 0.0f, 6.0f, "%.2f texels");
            ImGui::SliderFloat("Shadow Softness", &rs.shadows.softness, 0.5f, 3.0f);
        }

        ImGui::Checkbox("Analytic Atmosphere", &rs.atmosphere.enabled);
        if (rs.atmosphere.enabled) {
            ImGui::SliderFloat("Atmos Density", &rs.atmosphere.density, 0.0f, 3.0f);
            ImGui::SliderFloat("Atmos Falloff", &rs.atmosphere.heightFalloff, 0.02f, 1.0f);
            ImGui::SliderFloat("Atmos Anisotropy", &rs.atmosphere.anisotropy, -0.5f, 0.85f);
            ImGui::SliderFloat("Sun Disk Size", &rs.atmosphere.sunDiskSize, 0.005f, 0.12f);
            ImGui::SliderFloat("Sun Disk Intensity", &rs.atmosphere.sunDiskIntensity, 1.0f, 40.0f);
            ImGui::SliderFloat("Sky Blend", &rs.atmosphere.skyBlend, 0.0f, 1.0f);
            ImGui::SliderFloat("Aerial Perspective", &rs.atmosphere.aerialPerspective, 0.0f, 1.0f);
        }

        ImGui::TextDisabled("IBL / Reflection Probe");
        ImGui::SliderFloat("IBL Diffuse", &rs.ibl.diffuseIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("IBL Specular", &rs.ibl.specularIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Reflection Boost", &rs.ibl.reflectionBoost, 0.0f, 3.0f);
        ImGui::SliderFloat("Probe Blend Weight", &rs.ibl.localProbeBlend, 0.0f, 1.0f);
        ImGui::Checkbox("Local Reflection Probe", &rs.reflectionProbe.enabled);
        if (rs.reflectionProbe.enabled) {
            ImGui::DragFloat3("Probe Center", &rs.reflectionProbe.center.x, 0.1f);
            ImGui::DragFloat3("Probe Extents", &rs.reflectionProbe.extents.x, 0.1f, 0.5f, 200.0f);
            ImGui::SliderFloat("Probe Intensity", &rs.reflectionProbe.intensity, 0.0f, 4.0f);
            ImGui::SliderFloat("Probe Edge Blend", &rs.reflectionProbe.blend, 0.0f, 1.0f);
        }
    }
    ImGui::End();
}
} // namespace Demon
