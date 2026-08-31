// ==============================================================================
//  DemonEngine::OSServices
// ==============================================================================
#include "OSServices.h"
#include "Logger.h"

#include <Shobjidl.h>

namespace Demon {

namespace {

struct ComInit {
    HRESULT hr;
    explicit ComInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
    [[nodiscard]] bool ok() const { return SUCCEEDED(hr); }
};

std::wstring toWide(std::string_view text)
{
    if (text.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), out.data(), len);
    return out;
}

std::string toUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::optional<std::filesystem::path> runFileDialog(
    IFileDialog* dialog,
    std::wstring_view title,
    std::span<const DialogFilter> filters,
    std::wstring_view defaultExt)
{
    if (!dialog)
        return std::nullopt;

    if (!title.empty())
        dialog->SetTitle(title.data());

    if (!defaultExt.empty())
        dialog->SetDefaultExtension(defaultExt.data());

    if (!filters.empty()) {
        std::vector<COMDLG_FILTERSPEC> specs;
        specs.reserve(filters.size());
        for (const auto& f : filters)
            specs.push_back({ f.name.c_str(), f.spec.c_str() });
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);
    }

    HRESULT hr = dialog->Show(nullptr);
    if (FAILED(hr))
        return std::nullopt;

    Microsoft::WRL::ComPtr<IShellItem> item;
    hr = dialog->GetResult(&item);
    if (FAILED(hr))
        return std::nullopt;

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || !path)
        return std::nullopt;

    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace

bool OSServices::setClipboardText(std::string_view text)
{
    if (!OpenClipboard(nullptr))
        return false;
    EmptyClipboard();

    std::wstring wtext = toWide(text);
    const size_t bytes = (wtext.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle) {
        CloseClipboard();
        return false;
    }

    void* data = GlobalLock(handle);
    if (!data) {
        GlobalFree(handle);
        CloseClipboard();
        return false;
    }

    std::memcpy(data, wtext.c_str(), bytes);
    GlobalUnlock(handle);

    SetClipboardData(CF_UNICODETEXT, handle);
    CloseClipboard();
    return true;
}

std::string OSServices::getClipboardText()
{
    if (!OpenClipboard(nullptr))
        return {};

    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return {};
    }

    const wchar_t* wtext = static_cast<const wchar_t*>(GlobalLock(data));
    if (!wtext) {
        CloseClipboard();
        return {};
    }

    std::wstring temp(wtext);
    GlobalUnlock(data);
    CloseClipboard();
    return toUtf8(temp);
}

std::optional<std::filesystem::path> OSServices::openFileDialog(
    std::wstring_view title,
    std::span<const DialogFilter> filters)
{
    ComInit com;
    if (!com.ok())
        return std::nullopt;

    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        return std::nullopt;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM);

    return runFileDialog(dialog.Get(), title, filters, L"");
}

std::optional<std::filesystem::path> OSServices::saveFileDialog(
    std::wstring_view title,
    std::span<const DialogFilter> filters,
    std::wstring_view defaultExt)
{
    ComInit com;
    if (!com.ok())
        return std::nullopt;

    Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        return std::nullopt;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM);

    return runFileDialog(dialog.Get(), title, filters, defaultExt);
}

SystemInfo OSServices::querySystemInfo()
{
    SystemInfo info{};
    info.logicalCores = std::max(1u, std::thread::hardware_concurrency());

    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    info.cpuCores = sysInfo.dwNumberOfProcessors;

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        info.memoryMB = mem.ullTotalPhys / (1024ull * 1024ull);
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapterByGpuPreference(
                 i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            info.gpuName = toUtf8(desc.Description);
            break;
        }
    }

    return info;
}

} // namespace Demon
