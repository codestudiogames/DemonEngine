// =============================================================================
//  DemonGUI Editor Panels  —  Implementation
// =============================================================================
#include "EditorPanels.h"
#include "core/Logger.h"
#include "runtime/EditorRuntimeLayer.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdio>

namespace Demon::GUI {

static constexpr float k_rowH   = 22.f;
static constexpr float k_pad    = 6.f;
static constexpr float k_indent = 14.f;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void sectionHeader(GUIContext& ctx, Rect r, const std::string& title)
{
    ctx.drawRectGrad(r, {0.22f,0.22f,0.22f,1}, {0.18f,0.18f,0.18f,1});
    ctx.drawLine({r.x, r.y+r.h}, {r.x+r.w, r.y+r.h}, Palette::Accent, 1.5f);
    ctx.drawText({r.x+k_pad, r.y+(r.h-14.f)*0.5f}, title, Palette::TextBright);
}

// =============================================================================
//  ViewportPanel
// =============================================================================
void ViewportPanel::render(GUIContext& ctx, Rect c)
{
    m_content = c;
    if (!m_renderer) return;

    uint32_t w = static_cast<uint32_t>(c.w);
    uint32_t h = static_cast<uint32_t>(c.h);

    if ((w != m_lastW || h != m_lastH) && w > 8 && h > 8) {
        m_lastW = w; m_lastH = h;
        m_renderer->resizeViewport(w, h);
        if (m_scene) m_scene->onViewportResize(w, h);
    }

    GUITextureID vpTex = m_renderer->getViewportDescriptor();
    if (vpTex.ptr != 0)
        ctx.drawImage(vpTex, c);
    else {
        ctx.drawRectFilled(c, {0.05f, 0.05f, 0.08f, 1.f});
        ctx.drawTextCentered(c, "DX12 Viewport Initialising...", Palette::TextDisabled);
    }
}

glm::vec2 ViewportPanel::screenToNdc(Vec2 point) const
{
    if (m_content.w <= 0.0f || m_content.h <= 0.0f)
        return {0.0f, 0.0f};
    const float x = ((point.x - m_content.x) / m_content.w) * 2.0f - 1.0f;
    const float y = 1.0f - ((point.y - m_content.y) / m_content.h) * 2.0f;
    return {x, y};
}

// =============================================================================
//  SceneHierarchyPanel
// =============================================================================
void SceneHierarchyPanel::render(GUIContext& ctx, Rect c)
{
    // Header
    Rect hdr{c.x, c.y, c.w, 28.f};
    sectionHeader(ctx, hdr, "  Scene Hierarchy");

    // Add Entity button
    Rect addBtn{c.x + c.w - 80.f, c.y + 4.f, 74.f, 20.f};
    if (ctx.button(GUIContext::makeID("##addent"), addBtn, "+ Entity") && m_scene)
        m_scene->createEntity("Entity");

    if (!m_scene) return;

    Rect content{c.x, c.y + 28.f, c.w, c.h - 28.f};
    float contentH = 0.f;

    // Count total rows to know content height
    m_scene->forEachEntity([&](Entity e) { contentH += k_rowH; });

    ctx.beginScrollRegion(GUIContext::makeID("##scenescroll"), content, contentH, m_scrollY);

    float y = content.y - m_scrollY;
    m_scene->forEachEntity([&](Entity e) {
        drawEntity(ctx, e, y, content, 0);
    });

    ctx.endScrollRegion(GUIContext::makeID("##scenescroll"));
}

void SceneHierarchyPanel::drawEntity(GUIContext& ctx, Entity e,
                                      float& y, Rect content, int depth)
{
    if (y + k_rowH < content.y || y > content.y + content.h) { y += k_rowH; return; }

    auto* tc = e.hasComponent<TagComponent>() ? &e.getComponent<TagComponent>() : nullptr;
    std::string label = tc ? tc->tag : "Entity";
    uint64_t eid = static_cast<uint64_t>(e.getID());

    Rect row{content.x, y, content.w, k_rowH};
    bool& open = m_open[eid];
    bool selected = (m_selected == e);

    ctx.treeNode(GUIContext::makeID("##ent", int(eid)), row, label,
                 open, selected, depth);
    if (ctx.isClicked(row)) {
        m_selected = e;
        if (m_scene)
            m_scene->setSelectedEntity(e.getID());
    }

    y += k_rowH;
}

// =============================================================================
//  PropertiesPanel
// =============================================================================
void PropertiesPanel::render(GUIContext& ctx, Rect c)
{
    Rect hdr{c.x, c.y, c.w, 28.f};
    sectionHeader(ctx, hdr, "  Properties");

    if (!m_entity) {
        ctx.drawTextCentered({c.x, c.y+40, c.w, 30}, "No entity selected", Palette::TextDisabled);
        return;
    }

    auto* tc = m_entity.hasComponent<TagComponent>() ? &m_entity.getComponent<TagComponent>() : nullptr;
    if (tc) {
        Rect nr{c.x+k_pad, c.y+30, c.w-k_pad*2, k_rowH};
        std::string name = tc->tag;
        if (ctx.inputText(GUIContext::makeID("##entname"), nr, name))
            tc->tag = name;
    }

    Rect content{c.x, c.y + 56.f, c.w, c.h - 56.f};
    float contentH = 2800.f;
    ctx.beginScrollRegion(GUIContext::makeID("##propscroll"), content, contentH, m_scrollY);

    float y = content.y - m_scrollY;
    float rowW = c.w - k_pad*2;

    m_componentEditors.draw(ctx, m_entity, {c.x+k_pad, 0, rowW, k_rowH}, y);

    ctx.endScrollRegion(GUIContext::makeID("##propscroll"));
}

void PropertiesPanel::drawTransform(GUIContext& ctx, float& y, Rect r, TransformComponent& tc)
{
    Rect hdr{r.x-k_pad, y, r.w+k_pad*2, 22.f};
    sectionHeader(ctx, hdr, " Transform"); y += 24.f;

    auto row = [&]() -> Rect { Rect rr{r.x, y, r.w, k_rowH}; y += k_rowH + 2.f; return rr; };

    float pos[3] = {tc.translation.x, tc.translation.y, tc.translation.z};
    if (ctx.inputFloat3(GUIContext::makeID("##pos"), row(), "Position", pos))
        tc.translation = {pos[0], pos[1], pos[2]};

    float rot[3] = {tc.rotation.x, tc.rotation.y, tc.rotation.z};
    if (ctx.inputFloat3(GUIContext::makeID("##rot"), row(), "Rotation", rot))
        tc.rotation = {rot[0], rot[1], rot[2]};

    float scl[3] = {tc.scale.x, tc.scale.y, tc.scale.z};
    if (ctx.inputFloat3(GUIContext::makeID("##scl"), row(), "Scale", scl))
        tc.scale = {scl[0], scl[1], scl[2]};

    y += 6.f;
}

void PropertiesPanel::drawMeshRenderer(GUIContext& ctx, float& y, Rect r, MeshRendererComponent& mr)
{
    Rect hdr{r.x-k_pad, y, r.w+k_pad*2, 22.f};
    sectionHeader(ctx, hdr, " Mesh Renderer"); y += 24.f;

    Rect pathRow{r.x, y, r.w, k_rowH};
    ctx.drawText({r.x, y+4.f}, "Mesh:", Palette::TextDisabled);
    ctx.drawText({r.x+50.f, y+4.f}, mr.meshPath.empty() ? "(none)" : mr.meshPath, Palette::Text);
    y += k_rowH + 8.f;
}

void PropertiesPanel::drawCamera(GUIContext& ctx, float& y, Rect r, CameraComponent& cc)
{
    Rect hdr{r.x-k_pad, y, r.w+k_pad*2, 22.f};
    sectionHeader(ctx, hdr, " Camera"); y += 24.f;

    ctx.checkbox(GUIContext::makeID("##camprim"), {r.x, y, r.w, k_rowH}, "Primary", cc.primary);
    y += k_rowH + 2.f;

    float fov = cc.camera.getFovY();
    if (ctx.sliderFloat(GUIContext::makeID("##camfov"), {r.x, y, r.w, k_rowH}, "FOV", fov, 10.f, 170.f)) {
        cc.camera.setPerspective(fov,
                                 cc.camera.getAspect(),
                                 cc.camera.getNearClip(),
                                 cc.camera.getFarClip());
    }
    y += k_rowH + 2.f;

    float near_ = cc.camera.getNearClip();
    float far_ = cc.camera.getFarClip();
    if (ctx.sliderFloat(GUIContext::makeID("##camnear"), {r.x, y, r.w, k_rowH}, "Near", near_, 0.001f, 10.f)) {
        cc.camera.setPerspective(cc.camera.getFovY(),
                                 cc.camera.getAspect(),
                                 near_,
                                 cc.camera.getFarClip());
    }
    y += k_rowH + 2.f;
    if (ctx.sliderFloat(GUIContext::makeID("##camfar"), {r.x, y, r.w, k_rowH}, "Far", far_, 10.f, 10000.f)) {
        cc.camera.setPerspective(cc.camera.getFovY(),
                                 cc.camera.getAspect(),
                                 cc.camera.getNearClip(),
                                 far_);
    }
    y += k_rowH + 8.f;
}

void PropertiesPanel::drawLight(GUIContext& ctx, float& y, Rect r, LightComponent& lc)
{
    Rect hdr{r.x-k_pad, y, r.w+k_pad*2, 22.f};
    sectionHeader(ctx, hdr, " Light"); y += 24.f;

    float col[3] = {lc.color.r, lc.color.g, lc.color.b};
    if (ctx.colorEdit3(GUIContext::makeID("##lcolor"), {r.x, y, r.w, k_rowH}, "Color", col))
        lc.color = {col[0], col[1], col[2]};
    y += k_rowH + 2.f;

    ctx.sliderFloat(GUIContext::makeID("##lintens"), {r.x, y, r.w, k_rowH}, "Intensity",
                    lc.intensity, 0.f, 10.f);
    y += k_rowH + 8.f;
}

void PropertiesPanel::drawFog(GUIContext& ctx, float& y, Rect r, FogComponent& fc)
{
    Rect hdr{r.x-k_pad, y, r.w+k_pad*2, 22.f};
    sectionHeader(ctx, hdr, " Fog"); y += 24.f;

    ctx.checkbox(GUIContext::makeID("##fogenabled"), {r.x, y, r.w, k_rowH}, "Enabled", fc.enabled);
    y += k_rowH + 2.f;

    float col[3] = {fc.color.r, fc.color.g, fc.color.b};
    if (ctx.colorEdit3(GUIContext::makeID("##fogcol"), {r.x, y, r.w, k_rowH}, "Color", col))
        fc.color = {col[0], col[1], col[2]};
    y += k_rowH + 2.f;

    ctx.sliderFloat(GUIContext::makeID("##fogdens"), {r.x, y, r.w, k_rowH}, "Density",
                    fc.density, 0.f, 0.2f);
    y += k_rowH + 2.f;
    ctx.sliderFloat(GUIContext::makeID("##fogstart"), {r.x, y, r.w, k_rowH}, "Start",
                    fc.start, 0.f, 500.f);
    y += k_rowH + 8.f;
}

// =============================================================================
//  ContentBrowserPanel
// =============================================================================
ContentBrowserPanel::ContentBrowserPanel()
{
    m_root    = std::filesystem::current_path() / "assets";
    m_current = m_root;
}

void ContentBrowserPanel::setRootPath(const std::string& path)
{
    m_root = path; m_current = m_root;
}

void ContentBrowserPanel::render(GUIContext& ctx, Rect c)
{
    Rect hdr{c.x, c.y, c.w, 28.f};
    sectionHeader(ctx, hdr, "  Content Browser");

    // Filter bar
    Rect fb{c.x+k_pad, c.y+30.f, c.w-k_pad*2, k_rowH};
    ctx.inputText(GUIContext::makeID("##cbfilter"), fb, m_filter, "Filter...");

    Rect content{c.x, c.y+58.f, c.w, c.h-58.f};
    float contentH = 1000.f;
    ctx.beginScrollRegion(GUIContext::makeID("##cbscroll"), content, contentH, m_scrollY);

    float y = content.y - m_scrollY;
    if (std::filesystem::exists(m_root))
        drawDirectory(ctx, m_root, y, content, 0);

    ctx.endScrollRegion(GUIContext::makeID("##cbscroll"));

    if (ctx.isClicked(content, 1)) {
        m_contextMenuOpen = true;
        m_contextMenuPos = ctx.mousePos();
    }

    if (m_contextMenuOpen) {
        Rect menu{m_contextMenuPos.x, m_contextMenuPos.y, 150.f, 82.f};
        ctx.drawRectFilled(menu, Palette::Header);
        ctx.drawRect(menu, Palette::Accent);
        Rect newFolder{menu.x + 4.f, menu.y + 4.f, menu.w - 8.f, 22.f};
        Rect newMaterial{menu.x + 4.f, menu.y + 30.f, menu.w - 8.f, 22.f};
        Rect close{menu.x + 4.f, menu.y + 56.f, menu.w - 8.f, 22.f};

        if (ctx.button(GUIContext::makeID("##newfolder"), newFolder, "New Folder")) {
            std::error_code ec;
            std::filesystem::create_directories(m_current / ("New Folder " + std::to_string(m_createCounter++)), ec);
            m_contextMenuOpen = false;
        }
        if (ctx.button(GUIContext::makeID("##newmaterial"), newMaterial, "New Material")) {
            const auto path = m_current / ("New Material " + std::to_string(m_createCounter++) + ".material");
            std::ofstream file(path);
            file << "{\n  \"albedo\": [1, 1, 1, 1],\n  \"metallic\": 0,\n  \"roughness\": 0.5\n}\n";
            m_contextMenuOpen = false;
        }
        if (ctx.button(GUIContext::makeID("##closecontext"), close, "Close"))
            m_contextMenuOpen = false;
    }
}

void ContentBrowserPanel::drawDirectory(GUIContext& ctx,
                                         const std::filesystem::path& dir,
                                         float& y, Rect content, int depth)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (y + k_rowH < content.y) { y += k_rowH; continue; }
        if (y > content.y + content.h) break;

        std::string name = entry.path().filename().string();
        if (!m_filter.empty()) {
            std::string low = name;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low.find(m_filter) == std::string::npos && entry.is_directory()) {
                // still recurse into directories
            } else if (low.find(m_filter) == std::string::npos) {
                y += k_rowH; continue;
            }
        }

        Rect row{content.x, y, content.w, k_rowH};
        WidgetID id = GUIContext::makeID(entry.path().string().c_str());

        if (entry.is_directory()) {
            bool& open = m_dirOpen[entry.path().string()];
            ctx.treeNode(id, row, "▸  " + name, open, false, depth);
            y += k_rowH;
            if (open) drawDirectory(ctx, entry.path(), y, content, depth+1);
        } else {
            bool hov = ctx.isHovered(row);
            ctx.drawRectFilled(row, hov ? Palette::PanelHover : Palette::Panel);
            float indent = depth * k_indent + 20.f;
            ctx.drawText({row.x + indent, row.y + (row.h-14.f)*0.5f},
                         "  " + name, Palette::TextDisabled);
            y += k_rowH;
        }
    }
}

// =============================================================================
//  ConsolePanel
// =============================================================================
void ConsolePanel::pushMessage(const std::string& msg, int level)
{
    m_entries.push_back({msg, level});
    if (m_entries.size() > 2000) m_entries.erase(m_entries.begin());
    if (m_autoScroll) m_scrollY = 1e9f; // force scroll to bottom
}

void ConsolePanel::render(GUIContext& ctx, Rect c)
{
    // Header + buttons
    Rect hdr{c.x, c.y, c.w, 28.f};
    sectionHeader(ctx, hdr, "  Console");

    float bx = c.x + k_pad;
    auto mkBtn = [&](WidgetID id, const char* label, bool& toggle, Color on, Color off) {
        Rect br{bx, c.y + 30.f, 50.f, 18.f};
        if (ctx.button(id, br, label, toggle ? on : off)) toggle = !toggle;
        bx += 54.f;
    };

    mkBtn(GUIContext::makeID("##cinfo"), "INFO",  m_showInfo,  {0.2f,0.4f,0.7f,1}, Palette::Panel);
    mkBtn(GUIContext::makeID("##cwarn"), "WARN",  m_showWarn,  {0.6f,0.5f,0.1f,1}, Palette::Panel);
    mkBtn(GUIContext::makeID("##cerr"),  "ERROR", m_showError, {0.6f,0.2f,0.2f,1}, Palette::Panel);

    Rect clrBtn{c.x + c.w - 60.f, c.y + 30.f, 54.f, 18.f};
    if (ctx.button(GUIContext::makeID("##cclr"), clrBtn, "Clear")) clear();

    // Filter
    Rect fb{c.x+k_pad, c.y+52.f, c.w-k_pad*2, k_rowH};
    ctx.inputText(GUIContext::makeID("##conflt"), fb, m_filter, "Filter...");

    Rect content{c.x, c.y+78.f, c.w, c.h-78.f};
    float lineH = 16.f;
    float contentH = float(m_entries.size()) * lineH;
    ctx.beginScrollRegion(GUIContext::makeID("##conscroll"), content, contentH, m_scrollY);

    float y = content.y - m_scrollY;
    for (const auto& e : m_entries) {
        if (!m_showInfo  && e.level == 0) continue;
        if (!m_showWarn  && e.level == 1) continue;
        if (!m_showError && e.level == 2) continue;
        if (!m_filter.empty() && e.message.find(m_filter) == std::string::npos) {
            y += lineH; continue;
        }
        if (y + lineH < content.y) { y += lineH; continue; }
        if (y > content.y + content.h) break;

        Color col = e.level == 2 ? Palette::LogError :
                    e.level == 1 ? Palette::LogWarn  : Palette::LogInfo;
        ctx.drawText({content.x + k_pad, y + 1.f}, e.message, col);
        y += lineH;
    }
    ctx.endScrollRegion(GUIContext::makeID("##conscroll"));
}

// =============================================================================
//  EnvironmentPanel
// =============================================================================
void EnvironmentPanel::render(GUIContext& ctx, Rect c)
{
    Rect hdr{c.x, c.y, c.w, 28.f};
    sectionHeader(ctx, hdr, "  Environment & Lighting");

    Rect content{c.x, c.y+28.f, c.w, c.h-28.f};
    ctx.beginScrollRegion(GUIContext::makeID("##envscroll"), content, 600.f, m_scrollY);

    float y = content.y - m_scrollY;
    float rw = c.w - k_pad*2;
    auto row = [&]() -> Rect { Rect r{c.x+k_pad, y, rw, k_rowH}; y += k_rowH+2.f; return r; };
    auto sec = [&](const char* title, bool& open) -> bool {
        Rect hr{c.x, y, c.w, 22.f}; y += 24.f;
        sectionHeader(ctx, hr, title);
        bool clicked = ctx.isClicked(hr);
        if (clicked) open = !open;
        return open;
    };

    // ── Directional light ────────────────────────────────────────────────────
    if (sec("  Directional Light", m_lightOpen)) {
        ctx.colorEdit3(GUIContext::makeID("##eLC"), row(), "Color",     m_env.lightColor);
        ctx.sliderFloat(GUIContext::makeID("##eLI"), row(), "Intensity", m_env.lightIntensity, 0.f, 5.f);
        ctx.inputFloat3(GUIContext::makeID("##eLDir"), row(), "Direction", m_env.lightDir);
        ctx.colorEdit3(GUIContext::makeID("##eAmb"), row(), "Ambient",  m_env.ambient);
        y += 6.f;
    }

    // ── Skybox ────────────────────────────────────────────────────────────────
    if (sec("  Skybox", m_skyOpen)) {
        ctx.checkbox(GUIContext::makeID("##skyen"), row(), "Enabled", m_env.skyEnabled);
        std::string sp(m_env.skyPath);
        if (ctx.inputText(GUIContext::makeID("##skypath"), row(), sp, "Path to .hdr / .exr..."))
            strncpy(m_env.skyPath, sp.c_str(), sizeof(m_env.skyPath)-1);
        ctx.sliderFloat(GUIContext::makeID("##skyint"), row(), "Intensity", m_env.skyIntensity, 0.f, 5.f);

        if (m_renderer) {
            m_renderer->setSceneLighting(
                {m_env.lightDir[0],   m_env.lightDir[1],   m_env.lightDir[2]},
                {m_env.lightColor[0], m_env.lightColor[1], m_env.lightColor[2]},
                {m_env.ambient[0],    m_env.ambient[1],    m_env.ambient[2]});
            m_renderer->setSkybox(m_env.skyPath, m_env.skyIntensity, m_env.skyEnabled);
        }
        y += 6.f;
    }

    // ── Fog ───────────────────────────────────────────────────────────────────
    if (sec("  Fog", m_fogOpen)) {
        ctx.checkbox(GUIContext::makeID("##fogen"), row(), "Enabled", m_env.fogEnabled);
        ctx.colorEdit3(GUIContext::makeID("##fogcol"), row(), "Color",   m_env.fogColor);
        ctx.sliderFloat(GUIContext::makeID("##fogd"),  row(), "Density", m_env.fogDensity, 0.f, 0.1f);
        ctx.sliderFloat(GUIContext::makeID("##fogst"), row(), "Start",   m_env.fogStart,   0.f, 500.f);
        ctx.sliderFloat(GUIContext::makeID("##foght"), row(), "Height",  m_env.fogHeight, -50.f, 200.f);
        ctx.sliderFloat(GUIContext::makeID("##fogfall"), row(), "Falloff", m_env.fogFalloff, 0.f, 5.f);
        y += 6.f;
    }

    ctx.endScrollRegion(GUIContext::makeID("##envscroll"));
}

// =============================================================================
//  VolumetricsPanel
// =============================================================================
void VolumetricsPanel::render(GUIContext& ctx, Rect c)
{
    Rect hdr{c.x, c.y, c.w, 28.f};
    sectionHeader(ctx, hdr, "  Volumetrics");

    Rect content{c.x, c.y+28.f, c.w, c.h-28.f};
    ctx.beginScrollRegion(GUIContext::makeID("##volscroll"), content, 700.f, m_scrollY);

    float y = content.y - m_scrollY;
    float rw = c.w - k_pad*2;
    auto row = [&]() -> Rect { Rect r{c.x+k_pad, y, rw, k_rowH}; y += k_rowH+2.f; return r; };
    auto sec = [&](const char* title, bool& open) -> bool {
        Rect hr{c.x, y, c.w, 22.f}; y += 24.f;
        sectionHeader(ctx, hr, title);
        if (ctx.isClicked(hr)) open = !open;
        return open;
    };

    // ── Volumetric Fog ────────────────────────────────────────────────────────
    if (sec("  Volumetric Fog", m_volFogOpen)) {
        ctx.checkbox(GUIContext::makeID("##vfen"),   row(), "Enabled",  m_vol.volFogEnabled);
        ctx.colorEdit3(GUIContext::makeID("##vfc"),  row(), "Color",    m_vol.volFogColor);
        ctx.sliderFloat(GUIContext::makeID("##vfd"), row(), "Density",  m_vol.volFogDensity,  0.f, 0.5f);
        ctx.sliderFloat(GUIContext::makeID("##vfs"), row(), "Scatter",  m_vol.volFogScatter,  0.f, 1.f);
        ctx.sliderFloat(GUIContext::makeID("##vfh"), row(), "Height",   m_vol.volFogHeight,   0.f, 500.f);
        ctx.sliderFloat(GUIContext::makeID("##vff"), row(), "Falloff",  m_vol.volFogFalloff,  0.f, 0.1f);
        y += 6.f;
    }

    // ── Clouds ────────────────────────────────────────────────────────────────
    if (sec("  Volumetric Clouds", m_cloudsOpen)) {
        ctx.checkbox(GUIContext::makeID("##clen"),   row(), "Enabled",   m_vol.cloudsEnabled);
        ctx.sliderFloat(GUIContext::makeID("##clcov"),  row(), "Coverage",  m_vol.cloudCoverage, 0.f, 1.f);
        ctx.sliderFloat(GUIContext::makeID("##cldns"),  row(), "Density",   m_vol.cloudDensity,  0.f, 1.f);
        ctx.sliderFloat(GUIContext::makeID("##clspd"),  row(), "Speed",     m_vol.cloudSpeed,    0.f, 0.1f);
        ctx.sliderFloat(GUIContext::makeID("##clalt"),  row(), "Altitude",  m_vol.cloudAltitude, 0.f, 5000.f);
        ctx.sliderFloat(GUIContext::makeID("##clthk"),  row(), "Thickness", m_vol.cloudThickness, 10.f, 1000.f);
        ctx.colorEdit3(GUIContext::makeID("##clcol"),   row(), "Color",     m_vol.cloudColor);
        ctx.sliderFloat(GUIContext::makeID("##clshd"),  row(), "Shadow",    m_vol.cloudShadow,   0.f, 1.f);
        y += 6.f;
    }

    // ── God Rays ─────────────────────────────────────────────────────────────
    if (sec("  God Rays / Sun Shafts", m_godRaysOpen)) {
        ctx.checkbox(GUIContext::makeID("##gren"),    row(), "Enabled",   m_vol.godRaysEnabled);
        ctx.sliderFloat(GUIContext::makeID("##grint"), row(), "Intensity", m_vol.godRaysIntensity, 0.f, 2.f);
        ctx.sliderFloat(GUIContext::makeID("##grdc"),  row(), "Decay",     m_vol.godRaysDecay,    0.8f, 1.f);
        ctx.sliderFloat(GUIContext::makeID("##grdn"),  row(), "Density",   m_vol.godRaysDensity,  0.f, 1.f);
        y += 6.f;
    }

    ctx.endScrollRegion(GUIContext::makeID("##volscroll"));
}

} // namespace Demon::GUI
