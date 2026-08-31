#pragma once
// ==============================================================================
//  DemonEngine — Precompiled Header  (DemonPCH.h)
//  All heavy, stable external headers live here.
//  Internal engine headers are NOT included here to avoid circular deps.
// ==============================================================================

// ── Standard library ──────────────────────────────────────────────────────────
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// ── Windows (MinGW / MSVC) ────────────────────────────────────────────────────
#ifdef DEMON_PLATFORM_WINDOWS
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#endif

// ── DirectX 12 ────────────────────────────────────────────────────────────────
#if defined(__MINGW32__)
#   ifndef __REQUIRED_RPCNDR_H_VERSION__
#       define __REQUIRED_RPCNDR_H_VERSION__ 475
#   endif
#endif
#include <directx/d3d12.h>
#if defined(__MINGW32__)
#   include <dxguids/dxguids.h>
#endif
#include <dxgi1_6.h>
#include <wrl.h>

// ── GLM ───────────────────────────────────────────────────────────────────────
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

// ── DPI helpers (Win32) ───────────────────────────────────────────────────────
#ifdef DEMON_PLATFORM_WINDOWS
inline void DemonEnableDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
inline float DemonGetDpiScale(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    return float(dpi) / 96.0f;
}
#endif
