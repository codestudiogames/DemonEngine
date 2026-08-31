// ==============================================================================
//  DemonEngine::PlatformConfig
// ==============================================================================
#include "PlatformConfig.h"

namespace Demon {

void PlatformConfig::loadFrom(const RuntimeSettings& settings)
{
    bool value = false;
    if (settings.tryGetBool("platform.windows.enable_dpi_awareness", value))
        m_enableDpiAwareness = value;

    if (settings.tryGetBool("platform.windows.resizable", value)) {
        m_resizableOverride = true;
        m_resizable = value;
    }

    if (settings.tryGetBool("platform.windows.fullscreen", value)) {
        m_fullscreenOverride = true;
        m_fullscreen = value;
    }
}

void PlatformConfig::applyTo(WindowSpec& spec) const
{
    if (m_resizableOverride)
        spec.resizable = m_resizable;

    if (m_fullscreenOverride)
        spec.fullscreen = m_fullscreen;
}

} // namespace Demon
