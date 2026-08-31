#pragma once
// =============================================================================
//  DemonGUILayer  —  Engine Layer owning DemonGUI + all editor panels
//  Push this layer from DemonEditorApp instead of ExampleEditorLayer.
// =============================================================================
#include "core/Layer.h"
#include "core/EventSystem.h"
#include "DemonGUI/GUIContext.h"
#include "DemonGUI/GUIRenderer.h"
#include "DemonGUI/GUIDockSpace.h"
#include "EditorPanels.h"
#include "scene/Scene.h"

namespace Demon {

class EditorRuntimeLayer;

class DemonGUILayer : public Layer {
public:
    DemonGUILayer();
    ~DemonGUILayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(float dt) override;
    void onRender(Renderer& renderer) override;
    void onEvent(Event& e) override;

    void onGuiRender(Renderer& renderer, float displayW, float displayH) override;

    void setScene(std::shared_ptr<Scene> scene);
    void setRuntime(EditorRuntimeLayer* runtime);

private:
    void buildInput();
    void routeEvent(Event& e);

    // GUI system
    GUI::GUIContext   m_ctx;
    GUI::GUIRenderer  m_guiRenderer;
    GUI::GUIDockSpace m_dockSpace;
    GUI::GUIFont      m_font;

    // Editor panels
    GUI::ViewportPanel        m_viewport;
    GUI::SceneHierarchyPanel  m_hierarchy;
    GUI::PropertiesPanel      m_properties;
    GUI::ContentBrowserPanel  m_content;
    GUI::ConsolePanel         m_console;
    GUI::EnvironmentPanel     m_environment;
    GUI::VolumetricsPanel     m_volumetrics;
    GUI::MaterialEditorPanel  m_materialEditor;
    GUI::StatsPanel           m_stats;

    std::shared_ptr<Scene> m_scene;
    EditorRuntimeLayer* m_runtime = nullptr;

    // Input accumulation between frames
    GUI::GUIInput m_guiInput;
    float         m_displayW = 1280.f;
    float         m_displayH =  720.f;

    static constexpr const char* k_layoutPath = "demon_gui_layout.ini";
};

} // namespace Demon
