// ==============================================================================
//  DemonEngine::Components  –  Implementation
//  Utility functions for components that require non-inline logic.
// ==============================================================================
#include "Components.h"
#include "core/Logger.h"

namespace Demon {

    // TransformComponent and other structs are header-only (pure data + inline math).
    // This file is the correct place to add any future non-trivial component logic,
    // e.g. physics body sync, audio 3D positioning, animation state machine updates.

    // Example: validate a LightComponent at runtime
    void validateLight(const LightComponent& lc)
    {
        if (lc.intensity < 0.0f)
            DEMON_LOG_WARN("LightComponent: negative intensity ({:.2f}) — clamped.", lc.intensity);
        if (lc.type == LightType::Spot && lc.outerAngle <= lc.innerAngle)
            DEMON_LOG_WARN("LightComponent: spot outerAngle <= innerAngle.");
    }

} // namespace Demon