#pragma once
// ==============================================================================
//  DemonEngine::LayerStack
//  Hazel-style core abstraction for ordered gameplay layers and overlays.
//  Keeps layer insertion/removal policy out of Application.
// ==============================================================================
#include "DemonPCH.h"
#include "Layer.h"

namespace Demon {

class LayerStack {
public:
    using Container = std::vector<std::unique_ptr<Layer>>;
    using iterator = Container::iterator;
    using const_iterator = Container::const_iterator;
    using reverse_iterator = Container::reverse_iterator;
    using const_reverse_iterator = Container::const_reverse_iterator;

    LayerStack() = default;
    ~LayerStack() = default;

    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    void pushLayer(std::unique_ptr<Layer> layer)
    {
        if (!layer)
            return;

        layer->onAttach();
        m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(m_layerInsertIndex++), std::move(layer));
    }

    void pushOverlay(std::unique_ptr<Layer> overlay)
    {
        if (!overlay)
            return;

        overlay->onAttach();
        m_layers.emplace_back(std::move(overlay));
    }

    void clear()
    {
        for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) {
            if (*it)
                (*it)->onDetach();
        }

        m_layers.clear();
        m_layerInsertIndex = 0;
    }

    [[nodiscard]] bool empty() const { return m_layers.empty(); }
    [[nodiscard]] size_t size() const { return m_layers.size(); }

    iterator begin() { return m_layers.begin(); }
    iterator end() { return m_layers.end(); }
    const_iterator begin() const { return m_layers.begin(); }
    const_iterator end() const { return m_layers.end(); }
    const_iterator cbegin() const { return m_layers.cbegin(); }
    const_iterator cend() const { return m_layers.cend(); }

    reverse_iterator rbegin() { return m_layers.rbegin(); }
    reverse_iterator rend() { return m_layers.rend(); }
    const_reverse_iterator rbegin() const { return m_layers.rbegin(); }
    const_reverse_iterator rend() const { return m_layers.rend(); }
    const_reverse_iterator crbegin() const { return m_layers.crbegin(); }
    const_reverse_iterator crend() const { return m_layers.crend(); }

private:
    Container m_layers;
    uint32_t m_layerInsertIndex = 0;
};

} // namespace Demon
