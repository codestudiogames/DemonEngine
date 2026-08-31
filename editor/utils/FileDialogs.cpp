#include "FileDialogs.h"

#include <commdlg.h>
#include <shlobj.h>

namespace Demon::FileDialogs {
namespace {

std::optional<std::string> showFileDialog(bool save,
                                          const char* filter,
                                          const char* defaultExt,
                                          const char* initialDir)
{
    char path[MAX_PATH]{};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = filter && *filter ? filter : "All Files\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = initialDir;
    dialog.lpstrDefExt = defaultExt;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL accepted = save ? GetSaveFileNameA(&dialog) : GetOpenFileNameA(&dialog);
    if (!accepted)
        return std::nullopt;
    return std::string(path);
}

int CALLBACK browseCallback(HWND hwnd, UINT message, LPARAM, LPARAM data)
{
    if (message == BFFM_INITIALIZED && data != 0)
        SendMessageA(hwnd, BFFM_SETSELECTIONA, TRUE, data);
    return 0;
}

} // namespace

std::optional<std::string> openFile(const char* filter, const char* initialDir)
{
    return showFileDialog(false, filter, nullptr, initialDir);
}

std::optional<std::string> saveFile(const char* filter, const char* defaultExt, const char* initialDir)
{
    return showFileDialog(true, filter, defaultExt, initialDir);
}

std::optional<std::string> selectFolder(const char* title)
{
    BROWSEINFOA info{};
    info.hwndOwner = GetActiveWindow();
    info.lpszTitle = title ? title : "Select Folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    info.lpfn = browseCallback;

    PIDLIST_ABSOLUTE item = SHBrowseForFolderA(&info);
    if (!item)
        return std::nullopt;

    char path[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListA(item, path) != FALSE;
    CoTaskMemFree(item);
    return ok ? std::optional<std::string>(path) : std::nullopt;
}

} // namespace Demon::FileDialogs
