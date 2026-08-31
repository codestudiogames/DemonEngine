// WaterPanel.h
#pragma once
#include "Core/DemonCore.h"
#include "WaterSystem.h"

namespace Demon
{
    class WaterPanel
    {
    public:
        WaterPanel() = default;
        void OnImGui(WaterSystem& sys);

    private:
        bool m_ShowAdvanced = false;
    };
}

// ===========================================================================
// WaterPanel.cpp
// ===========================================================================
// #include "WaterPanel.h"
// #include <imgui.h>

namespace Demon
{
    void WaterPanel::OnImGui(WaterSystem& sys)
    {
        if (!ImGui::Begin("Water System"))
        {
            ImGui::End();
            return;
        }

        bool enabled = sys.IsEnabled();
        if (ImGui::Checkbox("Enable Water", &enabled))
            sys.SetEnabled(enabled);

        ImGui::Separator();

        WaterSettings& s = sys.GetGlobalSettings();
        FFTOcean& fft    = sys.GetFFTOcean();

        // ---- Simulation ----
        if (ImGui::CollapsingHeader("Wave Simulation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat("Wind Speed (m/s)",    &s.WindSpeed,     0.1f, 0.f, 50.f);
            ImGui::DragFloat("Wind Direction (deg)",&s.WindDirection, 1.f, 0.f, 360.f);
            ImGui::DragFloat("Wave Height",         &s.WaveHeight,    0.01f, 0.f, 5.f);
            ImGui::DragFloat("Choppiness",          &s.Choppiness,    0.01f, 0.f, 2.f);
            ImGui::DragFloat("Time Scale",          &s.TimeScale,     0.01f, 0.f, 4.f);
            ImGui::DragFloat("Patch Size (m)",      &s.PatchSize,     10.f, 50.f, 2000.f);
        }

        // ---- Surface Color ----
        if (ImGui::CollapsingHeader("Surface Color", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::ColorEdit4("Shallow Color",  &s.ShallowColor.x);
            ImGui::ColorEdit4("Deep Color",     &s.DeepColor.x);
            ImGui::ColorEdit4("Horizon Color",  &s.HorizonColor.x);
            ImGui::DragFloat("Depth Fade Start",&s.DepthFadeStart, 0.05f, 0.f, 20.f);
            ImGui::DragFloat("Depth Fade End",  &s.DepthFadeEnd,   0.1f, 0.f, 50.f);
            ImGui::DragFloat("Roughness",       &s.Roughness,      0.001f, 0.f, 1.f);
        }

        // ---- Normals ----
        if (ImGui::CollapsingHeader("Normals"))
        {
            ImGui::DragFloat("FFT Normal Strength",   &s.FFTNormalStrength,   0.01f, 0.f, 3.f);
            ImGui::DragFloat("Detail Normal Strength",&s.DetailNormalStrength,0.01f, 0.f, 2.f);
            ImGui::DragFloat("Normal Tile 0",         &s.NormalTile0,         0.05f, 0.1f, 10.f);
            ImGui::DragFloat("Normal Tile 1",         &s.NormalTile1,         0.05f, 0.1f, 10.f);
            ImGui::DragFloat2("Normal Scroll 0",      &s.NormalScroll0.x,     0.002f);
            ImGui::DragFloat2("Normal Scroll 1",      &s.NormalScroll1.x,     0.002f);
        }

        // ---- Reflections ----
        if (ImGui::CollapsingHeader("Reflections"))
        {
            ImGui::Checkbox("Planar Reflection",     &s.PlanarReflection);
            if (s.PlanarReflection)
                ImGui::DragFloat("Planar Blend",     &s.PlanarReflBlend, 0.01f, 0.f, 1.f);

            ImGui::Checkbox("Screen-Space Reflections", &s.SSREnabled);
            if (s.SSREnabled)
            {
                ImGui::DragFloat("SSR Intensity",    &s.SSRIntensity,   0.01f, 0.f, 1.f);
                ImGui::DragFloat("SSR Max Distance", &s.SSRMaxDistance, 1.f,   0.f, 200.f);
                ImGui::DragFloat("SSR Thickness",    &s.SSRThickness,   0.05f, 0.f, 5.f);
            }

            ImGui::DragFloat("Refraction Strength",  &s.RefractionStrength, 0.001f, 0.f, 0.2f);
            ImGui::DragFloat("Refraction IOR",       &s.RefractionIndex,    0.001f, 1.f, 2.f);
        }

        // ---- Foam ----
        if (ImGui::CollapsingHeader("Foam"))
        {
            ImGui::Checkbox("Enable Foam",         &s.FoamEnabled);
            if (s.FoamEnabled)
            {
                ImGui::DragFloat("Foam Threshold", &s.FoamThreshold, 0.01f, 0.f, 1.f);
                ImGui::DragFloat("Foam Intensity", &s.FoamIntensity, 0.01f, 0.f, 3.f);
                ImGui::DragFloat("Shore Foam Depth",&s.FoamFadeDepth,0.05f, 0.f, 5.f);
                ImGui::ColorEdit3("Foam Color",    &s.FoamColor.x);
            }
        }

        // ---- Caustics ----
        if (ImGui::CollapsingHeader("Caustics"))
        {
            ImGui::Checkbox("Enable Caustics",        &s.CausticsEnabled);
            if (s.CausticsEnabled)
            {
                ImGui::DragFloat("Scale",             &s.CausticsScale,    0.05f, 0.f, 5.f);
                ImGui::DragFloat("Speed",             &s.CausticsSpeed,    0.01f, 0.f, 2.f);
                ImGui::DragFloat("Intensity",         &s.CausticsIntensity,0.01f, 0.f, 3.f);
            }
        }

        // ---- Underwater ----
        if (ImGui::CollapsingHeader("Underwater"))
        {
            ImGui::ColorEdit3("Fog Color",             &s.UnderwaterFogColor.x);
            ImGui::DragFloat("Fog Density",            &s.UnderwaterFogDensity, 0.001f, 0.f, 1.f);
            ImGui::ColorEdit4("Tint",                  &s.UnderwaterTint.x);
            ImGui::DragFloat("Caustic Intensity",      &s.UnderwaterCausticIntensity, 0.01f, 0.f, 5.f);
        }

        // ---- Debug ----
        if (ImGui::CollapsingHeader("Debug / Info"))
        {
            ImGui::Text("FFT Resolution: %d x %d", fft.GetResolution(), fft.GetResolution());
            ImGui::Text("Patch Size: %.0f m",       fft.GetPatchSize());
            ImGui::Separator();
            ImGui::Text("Displacement Map: ID3D12Resource* %p", (void*)fft.GetDisplacementMap());
            ImGui::Text("Normal Map:       ID3D12Resource* %p", (void*)fft.GetNormalMap());
            ImGui::Text("Foam Map:         ID3D12Resource* %p", (void*)fft.GetFoamMap());
        }

        ImGui::End();
    }
}
