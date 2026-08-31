#pragma once
// ==============================================================================
//  DemonEngine::PlatformConfig
//  Windows-specific overrides sourced from RuntimeSettings.
// ==============================================================================
#include "DemonPCH.h"
#include "RuntimeSettings.h"
#include "Window.h"

namespace Demon {

class PlatformConfig {
public:
    void loadFrom(const RuntimeSettings& settings);
    void applyTo(WindowSpec& spec) const;

    [[nodiscard]] bool enableDpiAwareness() const { return m_enableDpiAwareness; }

private:
    bool m_enableDpiAwareness = true;
    bool m_resizableOverride = false;
    bool m_resizable = true;
    bool m_fullscreenOverride = false;
    bool m_fullscreen = false;
};

} // namespace Demon
