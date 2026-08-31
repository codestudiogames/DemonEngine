#pragma once
// ==============================================================================
//  DemonEngine Editor::EditorSettings
//  Shared editor runtime flags (header-only for simplicity).
// ==============================================================================
#include <imgui.h>
#include <imgui_internal.h>
#include "../engine/core/DemonPCH.h"

namespace Demon {

// Fallbacks for non-docking ImGui builds
#ifndef ImGuiWindowFlags_NoDocking
#define ImGuiWindowFlags_NoDocking 0
#endif
#ifndef ImGuiDockNodeFlags_None
typedef int ImGuiDockNodeFlags;
enum {
    ImGuiDockNodeFlags_None      = 0,
    ImGuiDockNodeFlags_NoDocking = 1 << 0,
    ImGuiDockNodeFlags_NoSplit   = 1 << 1,
    ImGuiDockNodeFlags_NoResize  = 1 << 2
};
#endif

struct EditorSettings {
    static inline bool dockingLocked = false;
};

inline ImGuiWindowFlags editorPanelFlags() {
    if (!EditorSettings::dockingLocked) return 0;
    return ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
}

inline ImGuiDockNodeFlags editorDockspaceFlags() {
    if (!EditorSettings::dockingLocked) return ImGuiDockNodeFlags_None;
    ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_NoDocking;
#ifdef ImGuiDockNodeFlags_NoSplit
    flags |= ImGuiDockNodeFlags_NoSplit;
#endif
#ifdef ImGuiDockNodeFlags_NoResize
    flags |= ImGuiDockNodeFlags_NoResize;
#endif
    return flags;
}

} // namespace Demon
