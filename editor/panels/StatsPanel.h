#pragma once
// ==============================================================================
//  DemonEngine Editor::StatsPanel
//  Displays render stats, frame time, FPS and memory info.
// ==============================================================================
#include "core/DemonPCH.h"
#include "renderer/Renderer.h"

namespace Demon {

class StatsPanel {
public:
    void render(const RenderStats& stats);

private:
    static constexpr int HISTORY = 120;
    float m_frameTimes[HISTORY]  = {};
    int   m_frameIdx             = 0;
    float m_fpsSmoothed          = 0.0f;
};

} // namespace Demon
