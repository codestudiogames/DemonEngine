#pragma once
// ==============================================================================
//  DemonEngine Editor::FileDialogs
//  Native Win32 open/save/folder pickers.
// ==============================================================================
#include "../engine/core/DemonPCH.h"

namespace Demon::FileDialogs {

    std::optional<std::string> openFile(const char* filter, const char* initialDir = nullptr);
    std::optional<std::string> saveFile(const char* filter, const char* defaultExt,
                                        const char* initialDir = nullptr);
    std::optional<std::string> selectFolder(const char* title);

} // namespace Demon::FileDialogs
