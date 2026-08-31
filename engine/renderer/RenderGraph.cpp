// ==============================================================================
//  DemonEngine::RenderGraph  –  Full Implementation
// ==============================================================================
#include "RenderGraph.h"
#include "core/Logger.h"
#include <queue>
#include <numeric>

namespace Demon {

RenderPassNode& RenderGraph::addPass(const std::string& name,
                                      std::function<void(ID3D12GraphicsCommandList*)> executeFunc)
{
    RenderPassNode node;
    node.name    = name;
    node.execute = std::move(executeFunc);
    m_passes.push_back(std::move(node));
    return m_passes.back();
}

void RenderGraph::setOutput(const std::string& passName)
{
    m_outputPass = passName;
}

void RenderGraph::clear()
{
    m_passes.clear();
    m_outputPass.clear();
}

// ── Topological sort (Kahn's algorithm) ──────────────────────────────────────
std::vector<size_t> RenderGraph::topoSort() const
{
    size_t n = m_passes.size();
    std::vector<size_t> order;
    order.reserve(n);

    // Build a resource → producer map
    std::unordered_map<std::string, size_t> producer;
    for (size_t i = 0; i < n; ++i)
        for (auto& w : m_passes[i].writes)
            producer[w] = i;

    // Build adjacency and in-degree
    std::vector<std::vector<size_t>> adj(n);
    std::vector<int> inDegree(n, 0);

    for (size_t i = 0; i < n; ++i) {
        for (auto& r : m_passes[i].reads) {
            auto it = producer.find(r);
            if (it != producer.end() && it->second != i) {
                adj[it->second].push_back(i);
                ++inDegree[i];
            }
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < n; ++i)
        if (inDegree[i] == 0) q.push(i);

    while (!q.empty()) {
        size_t cur = q.front(); q.pop();
        order.push_back(cur);
        for (size_t next : adj[cur])
            if (--inDegree[next] == 0) q.push(next);
    }

    if (order.size() != n)
        DEMON_LOG_WARN("RenderGraph: cycle detected — executing in declaration order.");

    return order.size() == n ? order : [&]{
        std::vector<size_t> v(n); std::iota(v.begin(), v.end(), 0); return v;
    }();
}

void RenderGraph::execute(ID3D12GraphicsCommandList* cmd)
{
    auto order = topoSort();
    for (size_t idx : order) {
        auto& pass = m_passes[idx];
        if (!pass.enabled || !pass.execute) continue;
        pass.execute(cmd);
    }
}

} // namespace Demon
