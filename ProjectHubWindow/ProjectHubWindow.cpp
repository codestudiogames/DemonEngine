#include "ProjectHubWindow.h"

#include "core/ProjectSettings.h"
#include "editor/utils/FileDialogs.h"

#include <algorithm>
#include <fstream>
#include <vector>
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace Demon::ProjectHub {
namespace {

constexpr wchar_t k_windowClass[] = L"DemonEngineProjectHub";

enum ControlId : int {
    IdRecentProjects = 100,
    IdProjectName,
    IdProjectLocation,
    IdBrowseLocation,
    IdCreateProject,
    IdOpenProject,
    IdCancel,
};

constexpr COLORREF k_background = RGB(30, 32, 36);
constexpr COLORREF k_sidebar = RGB(23, 25, 29);
constexpr COLORREF k_input = RGB(39, 42, 48);
constexpr COLORREF k_border = RGB(61, 66, 76);
constexpr COLORREF k_text = RGB(235, 238, 244);
constexpr COLORREF k_mutedText = RGB(156, 164, 178);
constexpr COLORREF k_accent = RGB(43, 118, 224);
constexpr COLORREF k_accentHover = RGB(61, 137, 242);

struct HubState {
    HWND window = nullptr;
    HWND recentList = nullptr;
    HWND nameEdit = nullptr;
    HWND locationEdit = nullptr;
    HFONT normalFont = nullptr;
    HFONT titleFont = nullptr;
    HFONT headingFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH sidebarBrush = nullptr;
    HBRUSH inputBrush = nullptr;
    std::vector<std::filesystem::path> recentProjects;
    std::optional<Result> result;
    bool running = true;
};

std::wstring widen(std::string_view text)
{
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring output(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), count);
    return output;
}

std::string narrow(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        output.data(), count, nullptr, nullptr);
    return output;
}

std::wstring controlText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

std::filesystem::path recentProjectsFile()
{
    std::wstring localAppData(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(),
                                                  static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size())
        return {};
    localAppData.resize(length);
    return std::filesystem::path(localAppData) / "DemonEngine" / "recent_projects.txt";
}

void loadRecentProjects(HubState& state)
{
    std::ifstream input(recentProjectsFile());
    std::string line;
    while (std::getline(input, line)) {
        std::filesystem::path path = widen(line);
        if (!path.empty() && std::filesystem::exists(path) &&
            std::ranges::find(state.recentProjects, path) == state.recentProjects.end())
        {
            state.recentProjects.push_back(std::move(path));
        }
    }
}

void rememberProject(const std::filesystem::path& configPath)
{
    const std::filesystem::path recentFile = recentProjectsFile();
    if (recentFile.empty())
        return;

    std::vector<std::filesystem::path> paths{configPath};
    std::ifstream input(recentFile);
    std::string line;
    while (std::getline(input, line) && paths.size() < 12) {
        std::filesystem::path path = widen(line);
        if (!path.empty() && path != configPath && std::filesystem::exists(path))
            paths.push_back(std::move(path));
    }

    std::error_code error;
    std::filesystem::create_directories(recentFile.parent_path(), error);
    std::ofstream output(recentFile, std::ios::trunc);
    for (const auto& path : paths)
        output << narrow(path.wstring()) << '\n';
}

bool validProjectName(std::wstring_view name)
{
    if (name.empty() || name == L"." || name == L"..")
        return false;
    constexpr std::wstring_view invalid = L"<>:\"/\\|?*";
    return name.find_first_of(invalid) == std::wstring_view::npos;
}

void finish(HubState& state, Result result)
{
    rememberProject(result.configPath);
    state.result = std::move(result);
    state.running = false;
    DestroyWindow(state.window);
    PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0, 0);
}

void openProject(HubState& state, const std::filesystem::path& configPath)
{
    if (configPath.empty() || !std::filesystem::exists(configPath)) {
        MessageBoxW(state.window, L"The selected project file does not exist.",
                    L"Open Project", MB_OK | MB_ICONERROR);
        return;
    }

    Result result;
    result.action = Action::Open;
    result.configPath = std::filesystem::absolute(configPath);
    result.projectDir = result.configPath.parent_path();
    result.name = result.configPath.stem().string();
    finish(state, std::move(result));
}

void browseForProject(HubState& state)
{
    const auto path = FileDialogs::openFile(
        "Demon Project (*.demonproj)\0*.demonproj\0Legacy Project (*.json)\0*.json\0All Files (*.*)\0*.*\0");
    if (path)
        openProject(state, *path);
}

void createProject(HubState& state)
{
    const std::wstring projectName = controlText(state.nameEdit);
    const std::wstring location = controlText(state.locationEdit);
    if (!validProjectName(projectName)) {
        MessageBoxW(state.window, L"Enter a valid project name.", L"Create Project",
                    MB_OK | MB_ICONWARNING);
        SetFocus(state.nameEdit);
        return;
    }
    if (location.empty()) {
        MessageBoxW(state.window, L"Choose a project location.", L"Create Project",
                    MB_OK | MB_ICONWARNING);
        SetFocus(state.locationEdit);
        return;
    }

    Result result;
    result.action = Action::Create;
    result.name = narrow(projectName);
    result.projectDir = std::filesystem::path(location) / projectName;
    result.configPath = result.projectDir / (projectName + L".demonproj");

    if (std::filesystem::exists(result.configPath)) {
        const int answer = MessageBoxW(state.window,
            L"This project already exists. Open it instead?", L"Existing Project",
            MB_YESNO | MB_ICONQUESTION);
        if (answer == IDYES)
            openProject(state, result.configPath);
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(result.projectDir / "assets" / "scenes", error);
    std::filesystem::create_directories(result.projectDir / "assets" / "materials", error);
    std::filesystem::create_directories(result.projectDir / "assets" / "scripts", error);
    if (error) {
        const std::wstring message = L"Could not create the project:\n" + widen(error.message());
        MessageBoxW(state.window, message.c_str(), L"Create Project", MB_OK | MB_ICONERROR);
        return;
    }

    ProjectSettings settings;
    settings.applyProjectDefaults(result.name);
    if (!settings.save(result.configPath)) {
        MessageBoxW(state.window, L"Could not write the project configuration.",
                    L"Create Project", MB_OK | MB_ICONERROR);
        return;
    }
    finish(state, std::move(result));
}

HWND addControl(HubState& state, const wchar_t* className, const wchar_t* text,
                DWORD style, int x, int y, int width, int height, int id, HFONT font)
{
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, state.window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
}

void createControls(HubState& state)
{
    addControl(state, L"STATIC", L"DEMON", SS_LEFT, 24, 28, 190, 44, 0, state.titleFont);
    addControl(state, L"STATIC", L"GAME ENGINE", SS_LEFT, 26, 70, 180, 24, 0, state.normalFont);
    addControl(state, L"STATIC", L"RECENT PROJECTS", SS_LEFT, 22, 119, 190, 22, 0, state.headingFont);

    state.recentList = addControl(state, L"LISTBOX", L"",
        LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT,
        20, 148, 210, 260, IdRecentProjects, state.normalFont);

    addControl(state, L"BUTTON", L"Open Existing...", BS_OWNERDRAW | WS_TABSTOP,
               20, 424, 210, 42, IdOpenProject, state.normalFont);

    addControl(state, L"STATIC", L"Create a new project", SS_LEFT,
               278, 43, 470, 38, 0, state.titleFont);
    addControl(state, L"STATIC", L"Choose a name and location. DemonEngine will create the standard asset folders.",
               SS_LEFT, 280, 86, 472, 42, 0, state.normalFont);
    addControl(state, L"STATIC", L"PROJECT NAME", SS_LEFT,
               280, 145, 220, 22, 0, state.headingFont);

    state.nameEdit = addControl(state, L"EDIT", L"DemonProject",
        ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
        280, 174, 470, 38, IdProjectName, state.normalFont);

    addControl(state, L"STATIC", L"LOCATION", SS_LEFT,
               280, 238, 220, 22, 0, state.headingFont);
    state.locationEdit = addControl(state, L"EDIT", std::filesystem::current_path().wstring().c_str(),
        ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER,
        280, 267, 376, 38, IdProjectLocation, state.normalFont);
    addControl(state, L"BUTTON", L"Browse", BS_OWNERDRAW | WS_TABSTOP,
               666, 267, 84, 38, IdBrowseLocation, state.normalFont);

    addControl(state, L"STATIC", L"The project configuration and Content Browser root will be created inside this folder.",
               SS_LEFT, 280, 319, 470, 42, 0, state.normalFont);

    addControl(state, L"BUTTON", L"Cancel", BS_OWNERDRAW | WS_TABSTOP,
               510, 424, 110, 42, IdCancel, state.normalFont);
    addControl(state, L"BUTTON", L"Create Project", BS_OWNERDRAW | WS_TABSTOP | BS_DEFPUSHBUTTON,
               632, 424, 118, 42, IdCreateProject, state.normalFont);

    for (HWND child = GetWindow(state.window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);

    for (const auto& project : state.recentProjects)
        SendMessageW(state.recentList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(project.wstring().c_str()));
    SetFocus(state.nameEdit);
    SendMessageW(state.nameEdit, EM_SETSEL, 0, -1);
}

void drawButton(const DRAWITEMSTRUCT& item)
{
    const bool primary = item.CtlID == IdCreateProject;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    COLORREF fill = primary ? k_accent : k_input;
    if (pressed)
        fill = primary ? RGB(31, 91, 180) : RGB(51, 55, 63);
    else if ((item.itemState & ODS_HOTLIGHT) != 0)
        fill = primary ? k_accentHover : RGB(48, 52, 60);

    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    FrameRect(item.hDC, &item.rcItem, GetSysColorBrush(disabled ? COLOR_GRAYTEXT : COLOR_3DSHADOW));

    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? k_mutedText : k_text);
    RECT textRect = item.rcItem;
    DrawTextW(item.hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focusRect = item.rcItem;
        InflateRect(&focusRect, -4, -4);
        DrawFocusRect(item.hDC, &focusRect);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* state = reinterpret_cast<HubState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<HubState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_CREATE:
            createControls(*state);
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IdCreateProject) {
                createProject(*state);
            } else if (id == IdOpenProject) {
                browseForProject(*state);
            } else if (id == IdBrowseLocation) {
                if (const auto folder = FileDialogs::selectFolder("Choose project location"))
                    SetWindowTextW(state->locationEdit, std::filesystem::path(*folder).wstring().c_str());
            } else if (id == IdCancel) {
                SendMessageW(window, WM_CLOSE, 0, 0);
            } else if (id == IdRecentProjects && HIWORD(wParam) == LBN_DBLCLK) {
                const LRESULT selection = SendMessageW(state->recentList, LB_GETCURSEL, 0, 0);
                if (selection != LB_ERR && static_cast<size_t>(selection) < state->recentProjects.size())
                    openProject(*state, state->recentProjects[static_cast<size_t>(selection)]);
            }
            return 0;
        }
        case WM_DRAWITEM:
            drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            RECT rect{};
            GetWindowRect(control, &rect);
            MapWindowPoints(HWND_DESKTOP, window, reinterpret_cast<POINT*>(&rect), 2);
            const bool onSidebar = rect.left < 250;
            SetBkColor(dc, onSidebar ? k_sidebar : k_background);
            SetTextColor(dc, k_text);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(onSidebar ? state->sidebarBrush : state->backgroundBrush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, k_input);
            SetTextColor(dc, k_text);
            return reinterpret_cast<LRESULT>(state->inputBrush);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, k_input);
            SetTextColor(dc, k_text);
            return reinterpret_cast<LRESULT>(state->inputBrush);
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(dc, &client, state->backgroundBrush);
            RECT sidebarRect{0, 0, 250, client.bottom};
            FillRect(dc, &sidebarRect, state->sidebarBrush);
            RECT accentRect{250, 0, client.right, 4};
            HBRUSH accentBrush = CreateSolidBrush(k_accent);
            FillRect(dc, &accentRect, accentBrush);
            DeleteObject(accentBrush);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CLOSE:
            state->running = false;
            DestroyWindow(window);
            PostThreadMessageW(GetCurrentThreadId(), WM_APP + 1, 0, 0);
            return 0;
        case WM_DESTROY:
            state->running = false;
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void centerWindow(HWND window)
{
    RECT rect{};
    GetWindowRect(window, &rect);
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    GetMonitorInfoW(monitor, &monitorInfo);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    const int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace

std::optional<Result> show()
{
    HubState state;
    loadRecentProjects(state);
    state.backgroundBrush = CreateSolidBrush(k_background);
    state.sidebarBrush = CreateSolidBrush(k_sidebar);
    state.inputBrush = CreateSolidBrush(k_input);
    state.normalFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    state.headingFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    state.titleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIcon = static_cast<HICON>(LoadImageW(nullptr, L"assets/icon/demon.ico",
                                                      IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = k_windowClass;
    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT,
        k_windowClass, L"DemonEngine Project Hub",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 510,
        nullptr, nullptr, windowClass.hInstance, &state);
    if (!window) {
        state.running = false;
    } else {
        BOOL darkTitleBar = TRUE;
        DwmSetWindowAttribute(window, 20, &darkTitleBar, sizeof(darkTitleBar));
        centerWindow(window);
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }

    MSG message{};
    while (state.running) {
        const BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0)
            break;
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (IsWindow(window))
        DestroyWindow(window);
    UnregisterClassW(k_windowClass, windowClass.hInstance);
    DeleteObject(state.normalFont);
    DeleteObject(state.headingFont);
    DeleteObject(state.titleFont);
    DeleteObject(state.backgroundBrush);
    DeleteObject(state.sidebarBrush);
    DeleteObject(state.inputBrush);
    return state.result;
}

} // namespace Demon::ProjectHub
