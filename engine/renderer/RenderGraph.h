#pragma once
// ==============================================================================
//  DemonEngine::RenderGraph
//  Lightweight frame graph: declare render passes with inputs/outputs,
//  then execute them in dependency order each frame.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

    class Renderer;

    // ── A single pass in the graph ────────────────────────────────────────────────
    struct RenderPassNode {
        std::string name;
        std::vector<std::string> reads;   // texture/resource inputs
        std::vector<std::string> writes;  // texture/resource outputs
        std::function<void(ID3D12GraphicsCommandList*)> execute;
        bool enabled = true;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    class RenderGraph {
    public:
        RenderGraph() = default;

        // ── Build ─────────────────────────────────────────────────────────────────
        // Add a pass; returns reference so you can chain .reads() / .writes()
        RenderPassNode& addPass(const std::string& name,
                                 std::function<void(ID3D12GraphicsCommandList*)> executeFunc);

        void setOutput(const std::string& passName);  // final pass that outputs to swapchain
        void clear();                                 // reset all passes

        // ── Execute ───────────────────────────────────────────────────────────────
        // Topologically sorts passes and records commands
        void execute(ID3D12GraphicsCommandList* cmd);

        [[nodiscard]] size_t passCount() const { return m_passes.size(); }

    private:
        std::vector<RenderPassNode> m_passes;
        std::string                 m_outputPass;

        std::vector<size_t> topoSort() const;
    };

} // namespace Demon
