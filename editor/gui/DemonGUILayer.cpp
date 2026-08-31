// =============================================================================
//  DemonGUILayer  —  Implementation
// =============================================================================
#include "DemonGUILayer.h"
#include "core/Application.h"
#include "input/Input.h"
#include "core/Logger.h"
#include "runtime/EditorRuntimeLayer.h"

namespace Demon {

DemonGUILayer::DemonGUILayer() : Layer("DemonGUI") {}
DemonGUILayer::~DemonGUILayer() = default;

void DemonGUILayer::onAttach()
{
    auto& app      = Application::get();
    auto& renderer = app.getRenderer();
    auto& ctx      = renderer.getContext();
    auto& srvHeap  = renderer.getSrvHeap();

    m_displayW = float(app.getWindow().getWidth());
    m_displayH = float(app.getWindow().getHeight());

    // ── Font ────────────────────────────────────────────────────────────────
    // allocate sampler heap from renderer
    // (GUIRenderer will create its own sampler slot internally)
    m_font.loadEmbedded(15.f, ctx, srvHeap);
    m_ctx.setFont(&m_font);

    // ── GUI Renderer ────────────────────────────────────────────────────────
    m_guiRenderer.init(ctx, srvHeap, renderer.getSamplerHeap(),
                       renderer.getSwapchainFormat(),
                       DX12Context::k_frameCount);
    m_guiRenderer.setFont(&m_font);

    // ── DockSpace ───────────────────────────────────────────────────────────
    m_dockSpace.init(m_displayW, m_displayH);

    // Register panels into slots
    // Left slot: hierarchy
    m_dockSpace.addPanel("hierarchy",   "Scene",        GUI::DockSlot::Left);
    // Centre slot: viewport
    m_dockSpace.addPanel("viewport",    "Viewport",     GUI::DockSlot::Centre);
    // Right slot: properties, environment, volumetrics
    m_dockSpace.addPanel("properties",  "Properties",   GUI::DockSlot::Right);
    m_dockSpace.addPanel("environment", "Environment",  GUI::DockSlot::Right);
    m_dockSpace.addPanel("volumetrics", "Volumetrics",  GUI::DockSlot::Right);
    m_dockSpace.addPanel("material",    "Material",     GUI::DockSlot::Right);
    // Bottom slot: console, content browser
    m_dockSpace.addPanel("console",     "Console",      GUI::DockSlot::Bottom);
    m_dockSpace.addPanel("content",     "Content",      GUI::DockSlot::Bottom);
    m_dockSpace.addPanel("stats",       "Stats",        GUI::DockSlot::Bottom);

    // Load saved layout or use defaults
    if (!m_dockSpace.loadLayout(k_layoutPath))
        DEMON_LOG_INFO("DemonGUILayer: using default layout.");

    // ── Panel setup ─────────────────────────────────────────────────────────
    m_viewport.setRenderer(&renderer);
    m_viewport.setRuntime(m_runtime);
    m_environment.setRenderer(&renderer);
    m_stats.setRenderer(&renderer);
    m_content.setRootPath("assets");

    m_ctx.init(m_displayW, m_displayH);

    DEMON_LOG_INFO("DemonGUILayer: attached.");
}

void DemonGUILayer::onDetach()
{
    m_dockSpace.saveLayout(k_layoutPath);
    m_guiRenderer.shutdown();
    m_font.destroy();
    DEMON_LOG_INFO("DemonGUILayer: layout saved, detached.");
}

void DemonGUILayer::onUpdate(float /*dt*/)
{
    const Entity selected = m_hierarchy.selectedEntity();
    m_properties.setSelectedEntity(selected);
    if (m_runtime)
        m_runtime->setSelectedEntity(selected ? selected.getID() : NULL_ENTITY);
}

void DemonGUILayer::onRender(Renderer& /*renderer*/)
{
    // Scene submission happens in the scene layer; GUI layer renders on top.
}

void DemonGUILayer::onEvent(Event& e)
{
    routeEvent(e);
}

void DemonGUILayer::setScene(std::shared_ptr<Scene> scene)
{
    m_scene = scene;
    m_hierarchy.setScene(scene);
    m_viewport.setScene(scene);
    m_materialEditor.setScene(scene);
}

void DemonGUILayer::setRuntime(EditorRuntimeLayer* runtime)
{
    m_runtime = runtime;
    m_viewport.setRuntime(runtime);
}

// ── Input accumulation ────────────────────────────────────────────────────────
void DemonGUILayer::routeEvent(Event& e)
{
    EventDispatcher d(e);

    d.dispatch<MouseMovedEvent>([&](MouseMovedEvent& ev) {
        m_guiInput.mousePos = {ev.x, ev.y};
        if (m_runtime && m_runtime->isTransformDragging() && m_viewport.contains({ev.x, ev.y})) {
            const glm::vec2 ndc = m_viewport.screenToNdc({ev.x, ev.y});
            (void)m_runtime->updateTransformDrag(ndc.x, ndc.y);
        }
        return false; // don't consume — viewport camera also wants it
    });

    d.dispatch<MouseButtonPressedEvent>([&](MouseButtonPressedEvent& ev) {
        if (ev.button >= 0 && ev.button < 3) {
            m_guiInput.mouseDown[ev.button]  = true;
            m_guiInput.mouseClick[ev.button] = true;
        }
        if (ev.button == 0 && m_runtime && m_viewport.contains(m_guiInput.mousePos)) {
            const glm::vec2 ndc = m_viewport.screenToNdc(m_guiInput.mousePos);
            if (!m_runtime->beginTransformDrag(ndc.x, ndc.y)) {
                const EntityID picked = m_runtime->pickEntity(ndc.x, ndc.y);
                m_runtime->setSelectedEntity(picked);
                m_hierarchy.setSelectedEntity(picked);
                if (m_scene)
                    m_scene->setSelectedEntity(picked);
            }
        }
        return false;
    });

    d.dispatch<MouseButtonReleasedEvent>([&](MouseButtonReleasedEvent& ev) {
        if (ev.button >= 0 && ev.button < 3) {
            m_guiInput.mouseDown[ev.button]    = false;
            m_guiInput.mouseRelease[ev.button] = true;
        }
        if (ev.button == 0 && m_runtime)
            m_runtime->endTransformDrag();
        return false;
    });

    d.dispatch<MouseScrolledEvent>([&](MouseScrolledEvent& ev) {
        m_guiInput.scrollY = ev.yOffset;
        return false;
    });

    d.dispatch<KeyTypedEvent>([&](KeyTypedEvent& ev) {
        m_guiInput.keyChar = ev.codepoint;
        return false;
    });

    d.dispatch<KeyPressedEvent>([&](KeyPressedEvent& ev) {
        m_guiInput.keyBackspace = (ev.key == VK_BACK);
        m_guiInput.keyDelete    = (ev.key == VK_DELETE);
        m_guiInput.keyLeft      = (ev.key == VK_LEFT);
        m_guiInput.keyRight     = (ev.key == VK_RIGHT);
        m_guiInput.keyEnter     = (ev.key == VK_RETURN);
        m_guiInput.keyEscape    = (ev.key == VK_ESCAPE);
        m_guiInput.keyHome      = (ev.key == VK_HOME);
        m_guiInput.keyEnd       = (ev.key == VK_END);
        bool ctrl = (ev.mods & (1<<2)) != 0;
        m_guiInput.keyCtrlA = ctrl && (ev.key == 'A');
        m_guiInput.keyCtrlC = ctrl && (ev.key == 'C');
        m_guiInput.keyCtrlV = ctrl && (ev.key == 'V');
        m_guiInput.keyCtrlZ = ctrl && (ev.key == 'Z');
        if (m_runtime) {
            if (ev.key == 'W') m_runtime->setTransformTool(EditorRuntimeLayer::TransformTool::Translate);
            if (ev.key == 'E') m_runtime->setTransformTool(EditorRuntimeLayer::TransformTool::Rotate);
            if (ev.key == 'R') m_runtime->setTransformTool(EditorRuntimeLayer::TransformTool::Scale);
            if (ev.key == 'F') m_runtime->focusSelected();
            if (ev.key == 'G') m_runtime->setGridVisible(!m_runtime->isGridVisible());
        }
        return false;
    });
}

// ── Main render — called from Application run loop ────────────────────────────
void DemonGUILayer::onGuiRender(Renderer& renderer, float displayW, float displayH)
{
    auto* cmd = renderer.getContext().getCommandList();
    uint32_t frameIndex = renderer.getFrameIndex();
    m_displayW = displayW;
    m_displayH = displayH;

    // Begin GUI frame
    m_ctx.beginFrame(m_guiInput, displayW, displayH);

    // Reset one-shot input flags
    for (int i = 0; i < 3; ++i) {
        m_guiInput.mouseClick[i]   = false;
        m_guiInput.mouseRelease[i] = false;
    }
    m_guiInput.scrollY     = 0.f;
    m_guiInput.keyChar     = 0;
    m_guiInput.keyBackspace= false;
    m_guiInput.keyDelete   = false;
    m_guiInput.keyLeft     = false;
    m_guiInput.keyRight    = false;
    m_guiInput.keyEnter    = false;
    m_guiInput.keyEscape   = false;

    // Draw dock layout background + tab bars
    m_dockSpace.beginFrame(m_ctx, displayW, displayH);

    m_ctx.drawRectFilled({0.f, 0.f, displayW, 24.f}, GUI::Palette::Header);
    if (m_runtime) {
        if (m_ctx.button(GUI::GUIContext::makeID("##play"), {8.f, 2.f, 54.f, 20.f},
                         m_runtime->isPlaying() ? "Stop" : "Play"))
            m_runtime->setPlaying(!m_runtime->isPlaying());
        if (m_ctx.button(GUI::GUIContext::makeID("##pause"), {66.f, 2.f, 58.f, 20.f}, "Pause"))
            m_runtime->setPaused(!m_runtime->isPaused());
        if (m_ctx.button(GUI::GUIContext::makeID("##move"), {134.f, 2.f, 48.f, 20.f}, "Move"))
            m_runtime->setTransformTool(EditorRuntimeLayer::TransformTool::Translate);
        if (m_ctx.button(GUI::GUIContext::makeID("##rotate"), {186.f, 2.f, 54.f, 20.f}, "Rotate"))
            m_runtime->setTransformTool(EditorRuntimeLayer::TransformTool::Rotate);
        if (m_ctx.button(GUI::GUIContext::makeID("##scale"), {244.f, 2.f, 48.f, 20.f}, "Scale"))
            m_runtime->setTransformTool(EditorRuntimeLayer::TransformTool::Scale);
        if (m_ctx.button(GUI::GUIContext::makeID("##grid"), {302.f, 2.f, 54.f, 20.f},
                         m_runtime->isGridVisible() ? "Grid" : "No Grid"))
            m_runtime->setGridVisible(!m_runtime->isGridVisible());
        if (m_ctx.button(GUI::GUIContext::makeID("##snap"), {360.f, 2.f, 48.f, 20.f},
                         m_runtime->isSnapEnabled() ? "Snap" : "Free"))
            m_runtime->setSnapEnabled(!m_runtime->isSnapEnabled());
    }

    // ── Viewport (Centre) ────────────────────────────────────────────────────
    if (m_dockSpace.isPanelActive(GUI::DockSlot::Centre, "viewport"))
        m_viewport.render(m_ctx, m_dockSpace.getContentRect(GUI::DockSlot::Centre));

    // ── Left: Scene Hierarchy ────────────────────────────────────────────────
    const auto& leftId = m_dockSpace.activePanelId(GUI::DockSlot::Left);
    if (leftId == "hierarchy")
        m_hierarchy.render(m_ctx, m_dockSpace.getContentRect(GUI::DockSlot::Left));

    // ── Right: Properties / Environment / Volumetrics ────────────────────────
    const auto& rightId = m_dockSpace.activePanelId(GUI::DockSlot::Right);
    GUI::Rect rightContent = m_dockSpace.getContentRect(GUI::DockSlot::Right);
    if      (rightId == "properties")  m_properties .render(m_ctx, rightContent);
    else if (rightId == "environment") m_environment.render(m_ctx, rightContent);
    else if (rightId == "volumetrics") m_volumetrics.render(m_ctx, rightContent);
    else if (rightId == "material")    m_materialEditor.render(m_ctx, rightContent);

    // ── Bottom: Console / Content Browser ────────────────────────────────────
    const auto& bottomId = m_dockSpace.activePanelId(GUI::DockSlot::Bottom);
    GUI::Rect bottomContent = m_dockSpace.getContentRect(GUI::DockSlot::Bottom);
    if      (bottomId == "console") m_console.render(m_ctx, bottomContent);
    else if (bottomId == "content") m_content.render(m_ctx, bottomContent);
    else if (bottomId == "stats")   m_stats.render(m_ctx, bottomContent);

    // Finalise draw list
    m_ctx.endFrame();

    // Flush to GPU
    m_guiRenderer.flush(cmd, m_ctx.drawList(), frameIndex, displayW, displayH);
}

} // namespace Demon
