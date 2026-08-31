#pragma once
// ==============================================================================
//  DemonEngine::Layer
//  Layers represent independent update/render slices of the application.
//  Game logic, editor UI, debug overlays etc. are each their own Layer.
// ==============================================================================
#include "EventSystem.h"

namespace Demon {
class Renderer;

class Layer {
public:
    explicit Layer(std::string name = "Layer") : m_name(std::move(name)) {}
    virtual ~Layer() = default;

    virtual void onAttach()  {}                       // called when pushed
    virtual void onDetach()  {}                       // called when popped
    virtual void onUpdate(float /*dt*/)  {}           // per-frame logic
    virtual void onRender(Renderer& /*r*/) {}         // per-frame render
    virtual void onGuiRender(Renderer& /*r*/, float /*displayW*/, float /*displayH*/) {}
    virtual void onEvent(Event& /*e*/) {}             // event handling

    [[nodiscard]] const std::string& getName() const { return m_name; }

protected:
    std::string m_name;
};

} // namespace Demon
