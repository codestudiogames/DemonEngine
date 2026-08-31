#pragma once
// ==============================================================================
//  DemonEngine::OSServices
//  Clipboard, dialogs, and system info helpers (Win32).
// ==============================================================================
#include "DemonPCH.h"

namespace Demon {

struct DialogFilter {
    std::wstring name;
    std::wstring spec;
};

struct SystemInfo {
    uint32_t cpuCores = 0;
    uint32_t logicalCores = 0;
    uint64_t memoryMB = 0;
    std::string gpuName;
};

class OSServices {
public:
    static bool setClipboardText(std::string_view text);
    static std::string getClipboardText();

    static std::optional<std::filesystem::path> openFileDialog(
        std::wstring_view title,
        std::span<const DialogFilter> filters = {});

    static std::optional<std::filesystem::path> saveFileDialog(
        std::wstring_view title,
        std::span<const DialogFilter> filters = {},
        std::wstring_view defaultExt = {});

    static SystemInfo querySystemInfo();
};

} // namespace Demon
