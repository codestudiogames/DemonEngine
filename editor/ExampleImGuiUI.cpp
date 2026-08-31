#include "ExampleImGuiUI.h"
#include "../engine/core/Application.h"

namespace Demon::ExampleImGuiUI {

void ApplyEditorStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                   = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    c[ImGuiCol_TextDisabled]           = ImVec4(0.56f, 0.56f, 0.56f, 1.00f);
    c[ImGuiCol_WindowBg]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_ChildBg]                = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_PopupBg]                = ImVec4(0.22f, 0.22f, 0.22f, 0.98f);
    c[ImGuiCol_Border]                 = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]                = ImVec4(0.23f, 0.23f, 0.23f, 1.00f);
    c[ImGuiCol_FrameBgHovered]         = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_FrameBgActive]          = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    c[ImGuiCol_TitleBg]                = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_TitleBgActive]          = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_MenuBarBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_ScrollbarBg]            = ImVec4(0.18f, 0.18f, 0.18f, 0.90f);
    c[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.42f, 0.42f, 0.42f, 1.00f);
    c[ImGuiCol_CheckMark]              = ImVec4(0.58f, 0.78f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]             = ImVec4(0.58f, 0.78f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrabActive]       = ImVec4(0.46f, 0.68f, 0.95f, 1.00f);
    c[ImGuiCol_Button]                 = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonActive]           = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
    c[ImGuiCol_Header]                 = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_HeaderActive]           = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
    c[ImGuiCol_Separator]              = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_SeparatorHovered]       = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    c[ImGuiCol_SeparatorActive]        = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    c[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    c[ImGuiCol_ResizeGripHovered]      = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    c[ImGuiCol_ResizeGripActive]       = ImVec4(0.52f, 0.52f, 0.52f, 1.00f);
    c[ImGuiCol_Tab]                    = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    c[ImGuiCol_TabHovered]             = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    c[ImGuiCol_TabActive]              = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
    c[ImGuiCol_TabUnfocused]           = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_PlotLines]              = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    c[ImGuiCol_PlotLinesHovered]       = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_PlotHistogram]          = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    c[ImGuiCol_TextSelectedBg]         = ImVec4(0.30f, 0.45f, 0.70f, 0.35f);
    c[ImGuiCol_DragDropTarget]         = ImVec4(0.58f, 0.78f, 1.00f, 0.90f);
    c[ImGuiCol_NavHighlight]           = ImVec4(0.58f, 0.78f, 1.00f, 1.00f);
    c[ImGuiCol_NavWindowingHighlight]  = ImVec4(0.58f, 0.78f, 1.00f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.04f, 0.04f, 0.05f, 0.65f);
    c[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.04f, 0.04f, 0.05f, 0.75f);
}

void DrawEditorUI(ImGuiIO& io, float mainScale)
{
    const float scale = mainScale;
#if defined(IMGUI_HAS_DOCK)
    const ImGuiWindowFlags no_docking_flag = ImGuiWindowFlags_NoDocking;
#else
    const ImGuiWindowFlags no_docking_flag = 0;
#endif

#if defined(IMGUI_HAS_DOCK)
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    }
#endif

    const float toolbar_h = 32.0f * scale;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbar_h));
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | no_docking_flag);
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * scale, 4.0f * scale));

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.32f, 0.55f, 0.32f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.62f, 0.38f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.46f, 0.26f, 1.00f));
        ImGui::Button(">", ImVec2(26.0f * scale, 0));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Play");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.46f, 0.46f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.34f, 0.34f, 0.34f, 1.00f));
        ImGui::Button("||", ImVec2(26.0f * scale, 0));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pause");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.46f, 0.46f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.34f, 0.34f, 0.34f, 1.00f));
        ImGui::Button(">>", ImVec2(26.0f * scale, 0));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Step");

        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::TextDisabled("Pivot");
        ImGui::SameLine();
        ImGui::Button("Center");
        ImGui::SameLine();
        ImGui::Button("Local");
        ImGui::SameLine();
        ImGui::Button("Global");
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::SameLine();
        ImGui::TextDisabled("Layout");
        ImGui::SameLine();
        ImGui::Button("2 by 3");
        ImGui::SameLine();
        ImGui::Button("Default");
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

#if defined(IMGUI_HAS_DOCK)
    ImGui::SetNextWindowPos(ImVec2(0.0f, toolbar_h));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - toolbar_h));
    ImGui::Begin("DockSpaceHost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | no_docking_flag | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    static bool dock_built = false;
    if (!dock_built)
    {
        dock_built = true;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetContentRegionAvail());

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.18f, nullptr, &dock_main);
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, nullptr, &dock_main);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.28f, nullptr, &dock_main);
        ImGuiID dock_stats = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.35f, nullptr, &dock_right);

        ImGui::DockBuilderDockWindow("Hierarchy", dock_left);
        ImGui::DockBuilderDockWindow("Project", dock_bottom);
        ImGui::DockBuilderDockWindow("Console", dock_bottom);
        ImGui::DockBuilderDockWindow("Inspector", dock_right);
        ImGui::DockBuilderDockWindow("Stats", dock_stats);
        ImGui::DockBuilderDockWindow("Post Processing", dock_stats);
        ImGui::DockBuilderDockWindow("Viewport", dock_main);

        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::End();
#endif

    ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Hierarchy", nullptr, panel_flags))
    {
        ImGui::TextDisabled("Scene");
        ImGui::Separator();
        if (ImGui::TreeNodeEx("World", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TreeNodeEx("Camera Rig", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            if (ImGui::TreeNodeEx("Player", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TreeNodeEx("Mesh: Explorer", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreeNodeEx("Controller", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Environment", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TreeNodeEx("Desert Rocks", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreeNodeEx("Outpost Tower", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreeNodeEx("Volumetric Fog", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Project", nullptr, panel_flags))
    {
        ImGui::TextDisabled("Assets");
        ImGui::Separator();
        const float thumb = 64.0f * scale;
        const float padding = 12.0f * scale;
        int columns = (int)(ImGui::GetContentRegionAvail().x / (thumb + padding));
        if (columns < 1) columns = 1;
        for (int i = 0; i < 24; ++i)
        {
            ImGui::PushID(i);
            ImGui::BeginGroup();
            ImGui::Button("##thumb", ImVec2(thumb, thumb));
            ImGui::TextUnformatted(i % 3 == 0 ? "mesh_crate" : (i % 3 == 1 ? "mat_brick" : "tex_gravel"));
            ImGui::EndGroup();
            ImGui::PopID();
            if ((i + 1) % columns != 0)
                ImGui::SameLine(0, padding);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Inspector", nullptr, panel_flags))
    {
        ImGui::TextDisabled("Selected");
        ImGui::TextUnformatted("Outpost Tower");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static float pos[3] = { 12.0f, 4.0f, -6.0f };
            static float rot[3] = { 0.0f, 45.0f, 0.0f };
            static float scale_v[3] = { 1.0f, 1.0f, 1.0f };
            ImGui::InputFloat3("Position", pos);
            ImGui::InputFloat3("Rotation", rot);
            ImGui::InputFloat3("Scale", scale_v);
        }
        if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static bool cast_shadows = true;
            static bool receive_shadows = true;
            ImGui::Checkbox("Cast Shadows", &cast_shadows);
            ImGui::Checkbox("Receive Shadows", &receive_shadows);
            static float roughness = 0.28f;
            static float metalness = 0.12f;
            ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metalness", &metalness, 0.0f, 1.0f);
        }
        if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static float albedo[3] = { 0.68f, 0.72f, 0.78f };
            ImGui::ColorEdit3("Albedo", albedo);
            ImGui::TextDisabled("Material: Brushed Alloy");
            ImGui::Button("Open Shader Graph");
        }
    }
    ImGui::End();

    if (ImGui::Begin("Viewport", nullptr, panel_flags))
    {
        if (ImGui::BeginTabBar("ViewportTabs"))
        {
            if (ImGui::BeginTabItem("Scene"))
            {
                ImVec2 size = ImGui::GetContentRegionAvail();
                if (size.x < 1.0f) size.x = 1.0f;
                if (size.y < 1.0f) size.y = 1.0f;

                static ImVec2 lastSize = ImVec2(0, 0);
                if (lastSize.x != size.x || lastSize.y != size.y) {
                    Application::get().getRenderer().resizeViewport(
                        static_cast<uint32_t>(size.x),
                        static_cast<uint32_t>(size.y));
                    lastSize = size;
                }

                ImTextureID tex = Application::get().getRenderer().getViewportDescriptor();
                if (tex) {
                    ImGui::Image(tex, size);
                } else {
                    ImGui::Dummy(size);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Game"))
            {
                ImGui::TextDisabled("Game View");
                ImGui::Separator();
                ImGui::TextUnformatted("Play mode preview goes here.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Console", nullptr, panel_flags))
    {
        ImGui::TextUnformatted("[Info] Build completed in 1.2s");
        ImGui::TextUnformatted("[Warn] Texture LOD mismatch: CliffRock_02");
        ImGui::TextUnformatted("[Info] Streaming complete");
    }
    ImGui::End();

    if (ImGui::Begin("Stats", nullptr, panel_flags))
    {
        ImGui::TextUnformatted("Rendering");
        ImGui::Separator();
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Text("Frame: %.2f ms", 1000.0f / io.Framerate);
        ImGui::TextUnformatted("Draw Calls: 184");
        ImGui::TextUnformatted("Triangles: 1.24M");
        ImGui::Separator();
        ImGui::TextUnformatted("Memory");
        ImGui::TextUnformatted("VRAM: 2.1 / 6.0 GB");
        ImGui::TextUnformatted("RAM: 3.4 / 16.0 GB");
    }
    ImGui::End();

    if (ImGui::Begin("Post Processing", nullptr, panel_flags))
    {
        auto& pp = Application::get().getRenderer().getPostProcessing();
        bool enabled = pp.isEnabled();
        if (ImGui::Checkbox("Enabled", &enabled))
            pp.setEnabled(enabled);

        auto& s = pp.settings();
        ImGui::Separator();

        ImGui::Checkbox("TAA", &s.taa.enabled);
        if (s.taa.enabled) {
            ImGui::SliderFloat("TAA Feedback", &s.taa.feedback, 0.0f, 1.0f);
            ImGui::SliderFloat("TAA Sharpness", &s.taa.sharpness, 0.0f, 1.0f);
            ImGui::SliderFloat("TAA Jitter", &s.taa.jitterScale, 0.25f, 2.0f);
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
        ImGui::Checkbox("DOF", &s.dof.enabled);
        if (s.dof.enabled) {
            ImGui::SliderFloat("Focus Distance", &s.dof.focusDistance, 0.01f, 1.0f);
            ImGui::SliderFloat("Focus Range", &s.dof.focusRange, 0.01f, 1.0f);
            ImGui::SliderFloat("Blur Strength", &s.dof.blurStrength, 0.1f, 3.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Bloom", &s.bloom.enabled);
        if (s.bloom.enabled) {
            ImGui::SliderFloat("Bloom Threshold", &s.bloom.threshold, 0.0f, 2.0f);
            ImGui::SliderFloat("Bloom Intensity", &s.bloom.intensity, 0.0f, 3.0f);
            ImGui::SliderFloat("Bloom Radius", &s.bloom.radius, 0.5f, 4.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Filmic Tonemap", &s.aces.enabled);
        if (s.aces.enabled) {
            int tonemapMode = (s.aces.mode == TonemapOperator::Reinhard) ? 1 : 0;
            const char* tonemapModes[] = { "ACES Filmic", "Reinhard" };
            if (ImGui::Combo("Tonemap Operator", &tonemapMode, tonemapModes, IM_ARRAYSIZE(tonemapModes)))
                s.aces.mode = (tonemapMode == 1) ? TonemapOperator::Reinhard : TonemapOperator::ACESFilm;
            ImGui::SliderFloat("Exposure", &s.aces.exposure, 0.1f, 4.0f);
        }
    }
    ImGui::End();
}

} // namespace Demon::ExampleImGuiUI
