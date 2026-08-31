#pragma once
// ==============================================================================
//  DemonEngine Editor::DemonTheme
//  Editor styling utilities.
// ==============================================================================
#include "core/DemonPCH.h"

struct ImFont;

namespace Demon {
class DemonTheme {
public:
    static void apply();
    static void applyFont(float size = 15.0f);

    // Monospace face used by the debug view overlay. Null when no monospace TTF
    // could be located on the host; callers must handle that. ImGui 1.92 sizes
    // fonts dynamically, so push it at whatever pixel size the overlay needs.
    static ImFont* monoFont()     { return s_monoFont; }
    static float   monoBaseSize() { return s_monoBaseSize; }

private:
    static ImFont* s_monoFont;
    static float   s_monoBaseSize;
};
} // namespace Demon