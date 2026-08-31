// ==============================================================================
//  DemonEngine Editor::MaterialEditorPanel  -  Native Win32 shell implementation
//
//  Architecture:
//    - A real Win32 HWND ("shell") is created with CreateWindowExA.
//    - Every frame, render() checks whether the shell window is visible and
//      alive, then uses ImGui::SetNextWindowPos/Size to pin the ImGui content
//      window exactly over the Win32 client area.
//    - ImGui draws into the main swapchain as usual; we just anchor it to the
//      shell window's screen position so it appears to live "inside" it.
//    - The shell handles WM_CLOSE (hides rather than destroys), WM_SIZE
//      (records new size), and WM_GETMINMAXINFO (minimum size constraints).
//    - No second DX12 device, no second swapchain, no ImGui multi-viewport.
// ==============================================================================
#include "MaterialEditorPanel.h"
#include "core/Logger.h"
#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/Texture.h"
#include "renderer/stb_image.h"
#include "../EditorSettings.h"
#include "../utils/TextBuffer.h"
#include <dwmapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace Demon {

// ── Static member definitions ─────────────────────────────────────────────────
bool MaterialEditorPanel::s_classRegistered = false;

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::string filenameOnly(const std::filesystem::path& p)
{
    return p.filename().string();
}

template<typename T = ImTextureID>
static std::enable_if_t<std::is_pointer_v<T>, ImTextureID> toImTextureID(uint64_t h)
{ return reinterpret_cast<ImTextureID>(h); }

template<typename T = ImTextureID>
static std::enable_if_t<!std::is_pointer_v<T>, ImTextureID> toImTextureID(uint64_t h)
{ return static_cast<ImTextureID>(h); }

struct CpuImage {
    std::vector<unsigned char> pixels;
    int w = 0, h = 0;
};

static bool loadCpuImage(const std::string& path, CpuImage& out)
{
    if (path.empty()) return false;
    auto img = stb_load_rgba(path.c_str());
    if (!img.pixels || img.w <= 0 || img.h <= 0) return false;
    out.w = img.w; out.h = img.h;
    out.pixels.assign(img.pixels, img.pixels + (img.w * img.h * 4));
    stb_free(img.pixels);
    return true;
}

static glm::vec4 sampleImage(const CpuImage& img, float u, float v)
{
    if (img.w <= 0 || img.h <= 0) return {1,1,1,1};
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    int x = static_cast<int>(u * (img.w - 1));
    int y = static_cast<int>(v * (img.h - 1));
    size_t idx = static_cast<size_t>(y * img.w + x) * 4;
    const float inv = 1.0f / 255.0f;
    return { img.pixels[idx+0]*inv, img.pixels[idx+1]*inv,
             img.pixels[idx+2]*inv, img.pixels[idx+3]*inv };
}

// ── Win32 shell window ────────────────────────────────────────────────────────
static constexpr const char* kShellClassName = "DemonMaterialEditorShell";

LRESULT CALLBACK MaterialEditorPanel::shellWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Store our panel pointer in GWLP_USERDATA on creation
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }

    auto* panel = reinterpret_cast<MaterialEditorPanel*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CLOSE:
        // Hide instead of destroy — docs stay alive, re-open just shows
        ShowWindow(hwnd, SW_HIDE);
        if (panel) panel->m_shellVisible = false;
        return 0;

    case WM_SIZE:
        if (panel && wp != SIZE_MINIMIZED) {
            panel->m_shellW = LOWORD(lp);
            panel->m_shellH = HIWORD(lp);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = { 400, 500 };
        return 0;
    }

    case WM_ERASEBKGND: {
        // Paint background dark so there's no white flash before ImGui draws
        HDC hdc = reinterpret_cast<HDC>(wp);
        RECT rc{}; GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(RGB(18, 18, 18));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }

    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

void MaterialEditorPanel::createShell()
{
    if (m_shellHwnd) return;

    HINSTANCE hInst = GetModuleHandleA(nullptr);

    // Register window class once
    if (!s_classRegistered) {
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = shellWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 18));
        wc.lpszClassName = kShellClassName;
        wc.hIcon         = static_cast<HICON>(
            LoadImageA(nullptr, "assets/icon/demon.ico",
                       IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED));
        RegisterClassExA(&wc);
        s_classRegistered = true;
    }

    // Centre on screen
    RECT work{}; SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    int x = (work.right  - work.left - m_shellW) / 2;
    int y = (work.bottom - work.top  - m_shellH) / 2;

    // Account for non-client area so the CLIENT area is exactly m_shellW x m_shellH
    RECT adjust{ 0, 0, m_shellW, m_shellH };
    DWORD style   = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = WS_EX_TOOLWINDOW;   // keeps it out of taskbar; remove if unwanted
    AdjustWindowRectEx(&adjust, style, FALSE, exStyle);
    int wW = adjust.right  - adjust.left;
    int wH = adjust.bottom - adjust.top;

    m_shellHwnd = CreateWindowExA(
        exStyle,
        kShellClassName,
        "DemonEngine  —  Material Editor",
        style,
        x, y, wW, wH,
        nullptr, nullptr, hInst,
        this);          // passed to WM_CREATE as lpCreateParams

    if (!m_shellHwnd) {
        DEMON_LOG_ERROR("MaterialEditorPanel: failed to create shell window (err={}).",
                        static_cast<uint32_t>(GetLastError()));
        return;
    }

    // Dark title bar (Windows 10 20H1+)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_shellHwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

void MaterialEditorPanel::destroyShell()
{
    if (m_shellHwnd) {
        DestroyWindow(m_shellHwnd);
        m_shellHwnd  = nullptr;
        m_shellVisible = false;
    }
}

void MaterialEditorPanel::showShell(bool show)
{
    if (!m_shellHwnd) createShell();
    if (!m_shellHwnd) return;
    ShowWindow(m_shellHwnd, show ? SW_SHOWNA : SW_HIDE);
    if (show) SetForegroundWindow(m_shellHwnd);
    m_shellVisible = show;
}

bool MaterialEditorPanel::getShellClientRect(float& ox, float& oy,
                                              float& ow, float& oh) const
{
    if (!m_shellHwnd || !IsWindowVisible(m_shellHwnd)) return false;
    RECT client{};
    if (!GetClientRect(m_shellHwnd, &client)) return false;
    POINT tl{ client.left, client.top };
    ClientToScreen(m_shellHwnd, &tl);
    ow = static_cast<float>(client.right  - client.left);
    oh = static_cast<float>(client.bottom - client.top);
    ox = static_cast<float>(tl.x);
    oy = static_cast<float>(tl.y);
    return ow > 1.0f && oh > 1.0f;
}

// ── Constructor / destructor ──────────────────────────────────────────────────
MaterialEditorPanel::MaterialEditorPanel()  = default;
MaterialEditorPanel::~MaterialEditorPanel() { destroyShell(); }

// ── Doc helpers ───────────────────────────────────────────────────────────────
MaterialEditorPanel::MaterialDoc*
MaterialEditorPanel::findDoc(const std::filesystem::path& path)
{
    auto norm = std::filesystem::weakly_canonical(path);
    for (auto& d : m_docs)
        if (std::filesystem::weakly_canonical(d.path) == norm)
            return &d;
    return nullptr;
}

bool MaterialEditorPanel::isImageFile(const std::filesystem::path& path)
{
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
        || ext == ".tga" || ext == ".bmp";
}

// ── File I/O ──────────────────────────────────────────────────────────────────
bool MaterialEditorPanel::loadFromFile(const std::filesystem::path& path,
                                        MaterialComponent& out)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        auto findStr = [&](const char* key) -> std::string {
            auto pos = line.find(std::string("\"") + key + "\"");
            if (pos == std::string::npos) return {};
            auto q1 = line.find('"', pos + 1);
            q1 = line.find('"', q1 + 1);
            auto q2 = line.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) return {};
            return line.substr(q1 + 1, q2 - q1 - 1);
        };
        auto findFloat = [&](const char* key) -> float {
            auto pos = line.find(std::string("\"") + key + "\"");
            if (pos == std::string::npos) return 0.0f;
            auto colon = line.find(':', pos);
            return std::stof(line.substr(colon + 1));
        };
        auto findVec3 = [&](const char* key) -> glm::vec3 {
            auto pos = line.find(std::string("\"") + key + "\"");
            if (pos == std::string::npos) return {};
            auto lb = line.find('[', pos), rb = line.find(']', lb);
            if (lb == std::string::npos || rb == std::string::npos) return {};
            std::istringstream ss(line.substr(lb+1, rb-lb-1));
            glm::vec3 v; char c;
            ss >> v.x >> c >> v.y >> c >> v.z;
            return v;
        };
        auto findVec4 = [&](const char* key) -> glm::vec4 {
            auto pos = line.find(std::string("\"") + key + "\"");
            if (pos == std::string::npos) return {};
            auto lb = line.find('[', pos), rb = line.find(']', lb);
            if (lb == std::string::npos || rb == std::string::npos) return {};
            std::istringstream ss(line.substr(lb+1, rb-lb-1));
            glm::vec4 v; char c;
            ss >> v.x >> c >> v.y >> c >> v.z >> c >> v.w;
            return v;
        };
        auto findBool = [&](const char* key) -> bool {
            auto pos = line.find(std::string("\"") + key + "\"");
            if (pos == std::string::npos) return false;
            auto colon = line.find(':', pos);
            return line.substr(colon+1).find("true") != std::string::npos;
        };

        if (line.find("\"albedo\"")          != std::string::npos) out.albedoColor     = findVec4("albedo");
        if (line.find("\"metallic\"")         != std::string::npos) out.metallic        = findFloat("metallic");
        if (line.find("\"roughness\"")        != std::string::npos) out.roughness       = findFloat("roughness");
        if (line.find("\"ao\"")               != std::string::npos) out.ao              = findFloat("ao");
        if (line.find("\"emissive\"")         != std::string::npos) out.emissiveColor   = findVec3("emissive");
        if (line.find("\"emissiveStrength\"") != std::string::npos) out.emissiveStrength= findFloat("emissiveStrength");
        if (line.find("\"doubleSided\"")      != std::string::npos) out.doubleSided     = findBool("doubleSided");
        if (line.find("\"alphaBlend\"")       != std::string::npos) out.alphaBlend      = findBool("alphaBlend");
        if (line.find("\"alphaCutoff\"")      != std::string::npos) out.alphaCutoff     = findFloat("alphaCutoff");
        if (line.find("\"albedoTex\"")        != std::string::npos) out.albedoTexture   = findStr("albedoTex");
        if (line.find("\"normalTex\"")        != std::string::npos) out.normalTexture   = findStr("normalTex");
        if (line.find("\"metallicTex\"")      != std::string::npos) out.metallicTexture = findStr("metallicTex");
        if (line.find("\"emissiveTex\"")      != std::string::npos) out.emissiveTexture = findStr("emissiveTex");
    }
    return true;
}

bool MaterialEditorPanel::saveToFile(const std::filesystem::path& path,
                                      const MaterialComponent& d)
{
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "{\n";
    out << "  \"albedo\": ["   << d.albedoColor.r  << "," << d.albedoColor.g
                                << "," << d.albedoColor.b << "," << d.albedoColor.a << "],\n";
    out << "  \"metallic\": "  << d.metallic        << ",\n";
    out << "  \"roughness\": " << d.roughness       << ",\n";
    out << "  \"ao\": "        << d.ao              << ",\n";
    out << "  \"emissive\": [" << d.emissiveColor.r << "," << d.emissiveColor.g
                                << "," << d.emissiveColor.b << "],\n";
    out << "  \"emissiveStrength\": " << d.emissiveStrength << ",\n";
    out << "  \"doubleSided\": " << (d.doubleSided ? "true" : "false") << ",\n";
    out << "  \"alphaBlend\": "  << (d.alphaBlend  ? "true" : "false") << ",\n";
    out << "  \"alphaCutoff\": " << d.alphaCutoff   << ",\n";
    out << "  \"albedoTex\": \""   << d.albedoTexture   << "\",\n";
    out << "  \"normalTex\": \""   << d.normalTexture   << "\",\n";
    out << "  \"metallicTex\": \"" << d.metallicTexture << "\",\n";
    out << "  \"emissiveTex\": \"" << d.emissiveTexture << "\"\n";
    out << "}\n";
    return true;
}

// ── Open / create ─────────────────────────────────────────────────────────────
bool MaterialEditorPanel::createAndOpen(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (!saveToFile(path, MaterialComponent{})) return false;
    openMaterial(path);
    return true;
}

void MaterialEditorPanel::openMaterial(const std::filesystem::path& path)
{
    if (auto* existing = findDoc(path)) {
        existing->open         = true;
        existing->requestFocus = true;
        showShell(true);
        return;
    }
    MaterialDoc doc{};
    doc.path = path;
    doc.data.materialPath = path.string();
    doc.requestFocus = true;
    if (!loadFromFile(path, doc.data))
        doc.dirty = true;
    m_docs.push_back(std::move(doc));
    showShell(true);
}

void MaterialEditorPanel::openMaterialForEntity(const std::filesystem::path& path,
                                                  Scene* scene, EntityID id)
{
    openMaterial(path);
    if (auto* doc = findDoc(path)) {
        doc->linkedScene  = scene;
        doc->linkedEntity = id;
        if (scene) {
            if (auto* mc = scene->getComponent<MaterialComponent>(id)) {
                if (!mc->materialPath.empty()) doc->path = mc->materialPath;
                MaterialComponent loaded;
                if (!mc->materialPath.empty() && loadFromFile(mc->materialPath, loaded)) {
                    doc->data = loaded;
                    doc->data.materialPath = mc->materialPath;
                    *mc = doc->data;
                    mc->materialLoaded = true;
                    mc->dirty = true;
                } else {
                    doc->data = *mc;
                }
            }
        }
    }
}

void MaterialEditorPanel::syncLinkedEntity(MaterialDoc& doc)
{
    if (!doc.linkedScene || doc.linkedEntity == NULL_ENTITY) return;
    if (auto* mc = doc.linkedScene->getComponent<MaterialComponent>(doc.linkedEntity)) {
        *mc = doc.data;
        mc->materialPath  = doc.path.string();
        mc->materialLoaded = true;
        mc->dirty = true;
    }
}

// ── Preview sphere ────────────────────────────────────────────────────────────
void MaterialEditorPanel::updatePreview(MaterialDoc& doc)
{
    if (!doc.previewDirty) return;

    const int size = 196;
    constexpr float kPi = 3.1415926535f;
    std::vector<unsigned char> rgba(static_cast<size_t>(size) * size * 4, 0);

    CpuImage albedoImg, normalImg, metalImg, emissiveImg;
    loadCpuImage(doc.data.albedoTexture,   albedoImg);
    loadCpuImage(doc.data.normalTexture,   normalImg);
    loadCpuImage(doc.data.metallicTexture, metalImg);
    loadCpuImage(doc.data.emissiveTexture, emissiveImg);

    glm::vec3 lightDir = glm::normalize(glm::vec3(0.3f, 0.8f, 0.6f));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float nx = (2.0f * x / (size-1)) - 1.0f;
            float ny = (2.0f * y / (size-1)) - 1.0f;
            float rr = nx*nx + ny*ny;
            size_t idx = static_cast<size_t>(y*size + x) * 4;
            if (rr > 1.0f) { rgba[idx+3] = 0; continue; }

            float nz = std::sqrt(1.0f - rr);
            glm::vec3 N = glm::normalize(glm::vec3(nx, -ny, nz));
            float phi   = std::atan2(N.z, N.x);
            float theta = std::acos(std::clamp(N.y, -1.0f, 1.0f));
            float u = (phi / (2.0f*kPi)) + 0.5f;
            float v = theta / kPi;

            if (!normalImg.pixels.empty()) {
                glm::vec4 nrm = sampleImage(normalImg, u, v);
                glm::vec3 n   = glm::vec3(nrm) * 2.0f - 1.0f;
                float sinTheta = std::sqrt(std::max(0.0001f, 1.0f - N.y*N.y));
                glm::vec3 T = glm::normalize(glm::vec3(-N.z, 0.0f, N.x));
                glm::vec3 B = glm::normalize(glm::vec3(
                    N.y*(N.x/sinTheta), -sinTheta, N.y*(N.z/sinTheta)));
                N = glm::normalize(glm::mat3(T,B,N) * n);
            }

            glm::vec4 albedo = doc.data.albedoColor;
            if (!albedoImg.pixels.empty()) albedo *= sampleImage(albedoImg, u, v);

            float rough = doc.data.roughness, metal = doc.data.metallic;
            if (!metalImg.pixels.empty()) {
                glm::vec4 mr = sampleImage(metalImg, u, v);
                rough = mr.g; metal = mr.b;
            }

            glm::vec3 base = glm::vec3(albedo);
            float diff = std::max(glm::dot(N, lightDir), 0.0f);
            glm::vec3 lit = base * (0.2f + diff*0.8f);
            lit = glm::mix(lit, base*0.04f, metal);
            lit *= glm::mix(1.0f, 0.7f, rough);
            lit *= doc.data.ao;

            if (!emissiveImg.pixels.empty())
                lit += glm::vec3(sampleImage(emissiveImg,u,v)) * doc.data.emissiveStrength;

            rgba[idx+0] = static_cast<unsigned char>(std::clamp(lit.r, 0.0f, 1.0f) * 255.0f);
            rgba[idx+1] = static_cast<unsigned char>(std::clamp(lit.g, 0.0f, 1.0f) * 255.0f);
            rgba[idx+2] = static_cast<unsigned char>(std::clamp(lit.b, 0.0f, 1.0f) * 255.0f);
            rgba[idx+3] = 255;
        }
    }

    auto& renderer = Application::get().getRenderer();
    doc.previewTexture = Texture::createFromRGBA(
        rgba.data(),
        static_cast<uint32_t>(size), static_cast<uint32_t>(size),
        renderer.getContext(), renderer.getSrvHeap(), false);

    doc.previewId = toImTextureID(0u);
    if (doc.previewTexture) {
        uint64_t gpuPtr = doc.previewTexture->getSrvGpuHandle().ptr;
        if (gpuPtr != 0) doc.previewId = toImTextureID(gpuPtr);
    }
    doc.previewDirty = false;
}

// ── Ctrl+S handler ────────────────────────────────────────────────────────────
bool MaterialEditorPanel::handleShortcutSave()
{
    if (!m_shellVisible) return false;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return false;
    if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_S)) {
        for (auto& d : m_docs) {
            if (!d.open || !d.focused) continue;
            if (saveToFile(d.path, d.data)) {
                d.dirty = false;
                syncLinkedEntity(d);
                DEMON_LOG_INFO("Material saved: '{}'", d.path.string());
            } else {
                DEMON_LOG_ERROR("Failed to save material: '{}'", d.path.string());
            }
            return true;
        }
    }
    return false;
}

// ── Per-doc ImGui rendering ───────────────────────────────────────────────────
void MaterialEditorPanel::renderDoc(MaterialDoc& doc)
{
    // Pin ImGui window to the Win32 shell client area.
    // The window has no title bar or padding — the Win32 shell provides those.
    float sx, sy, sw, sh;
    if (!getShellClientRect(sx, sy, sw, sh)) return;

    ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);

    if (doc.requestFocus) {
        ImGui::SetNextWindowFocus();
        doc.requestFocus = false;
    }

    // Tab label — filename + dirty marker
    std::string tabLabel = filenameOnly(doc.path);
    if (doc.dirty) tabLabel += " *";

    // Window flags: no decoration, no move, no resize — Win32 handles all that
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar    |
        ImGuiWindowFlags_NoResize      |
        ImGuiWindowFlags_NoMove        |
        ImGuiWindowFlags_NoCollapse    |
        ImGuiWindowFlags_NoSavedSettings;

    // Use a stable ID so all docs share the same pinned window (tab bar below)
    if (!ImGui::Begin("##MatEdShell", nullptr, flags)) {
        doc.focused = false;
        ImGui::End();
        return;
    }
    doc.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // ── Header: path + save button ────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::TextUnformatted(doc.path.string().c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::SmallButton("Save")) {
        if (saveToFile(doc.path, doc.data)) {
            doc.dirty = false;
            syncLinkedEntity(doc);
            DEMON_LOG_INFO("Material saved: '{}'", doc.path.string());
        }
    }
    ImGui::Separator();

    // ── Preview sphere ────────────────────────────────────────────────────────
    updatePreview(doc);
    if (doc.previewTexture && doc.previewId != static_cast<ImTextureID>(0)) {
        float available = ImGui::GetContentRegionAvail().x;
        float previewSz = std::min(196.0f, available);
        ImGui::SetCursorPosX((available - previewSz) * 0.5f + ImGui::GetCursorPosX());
        ImGui::Image(doc.previewId, ImVec2(previewSz, previewSz));
        ImGui::Separator();
    }

    auto editFlag = [&](bool changed) { if (changed) markDirty(doc); };

    // ── PBR sliders ───────────────────────────────────────────────────────────
    ImGui::PushItemWidth(-1);
    editFlag(ImGui::ColorEdit4("Albedo",    glm::value_ptr(doc.data.albedoColor)));
    editFlag(ImGui::DragFloat("Roughness",  &doc.data.roughness,  0.01f, 0.0f, 1.0f));
    editFlag(ImGui::DragFloat("Metallic",   &doc.data.metallic,   0.01f, 0.0f, 1.0f));
    ImGui::PopItemWidth();

    // ── Texture fields with drag-drop ─────────────────────────────────────────
    auto textureField = [&](const char* label, std::string& pathField) {
        char buf[256];
        copyStringToBuffer(buf, pathField);
        ImGui::PushItemWidth(-1);
        bool entered     = ImGui::InputText(label, buf, sizeof(buf),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
        if (entered || deactivated) { pathField = buf; markDirty(doc); }
        ImGui::PopItemWidth();
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                const char* p = static_cast<const char*>(payload->Data);
                if (p && isImageFile(std::filesystem::path(p))) {
                    pathField = p;
                    markDirty(doc);
                }
            }
            ImGui::EndDragDropTarget();
        }
    };

    textureField("Albedo Tex##mat", doc.data.albedoTexture);

    if (ImGui::CollapsingHeader("Advanced##matadv", ImGuiTreeNodeFlags_DefaultOpen)) {
        textureField("Normal Tex##mat",   doc.data.normalTexture);
        textureField("Metallic Tex##mat", doc.data.metallicTexture);
        textureField("Emissive Tex##mat", doc.data.emissiveTexture);
        ImGui::PushItemWidth(-1);
        editFlag(ImGui::ColorEdit3("Emissive Color##mat",    glm::value_ptr(doc.data.emissiveColor)));
        editFlag(ImGui::DragFloat("Emissive Strength##mat",  &doc.data.emissiveStrength, 0.01f, 0.0f, 10.0f));
        editFlag(ImGui::DragFloat("AO##mat",                 &doc.data.ao,               0.01f, 0.0f, 1.0f));
        editFlag(ImGui::Checkbox("Double Sided##mat",        &doc.data.doubleSided));
        editFlag(ImGui::Checkbox("Alpha Blend##mat",         &doc.data.alphaBlend));
        editFlag(ImGui::DragFloat("Alpha Cutoff##mat",       &doc.data.alphaCutoff,      0.01f, 0.0f, 1.0f));
        ImGui::PopItemWidth();
    }

    if (doc.dirty) doc.previewDirty = true;
    syncLinkedEntity(doc);
    ImGui::End();
}

// ── Main render entry point ───────────────────────────────────────────────────
void MaterialEditorPanel::render()
{
    // Pump Win32 messages for the shell window
    if (m_shellHwnd) {
        MSG msg{};
        while (PeekMessageA(&msg, m_shellHwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    // If shell isn't visible and there are no open docs, nothing to do
    if (!m_shellVisible || !m_shellHwnd || !IsWindowVisible(m_shellHwnd)) {
        // Still need to pump to catch WM_CLOSE etc.
        return;
    }

    // Render the active (first open) doc into the shell area.
    // If multiple docs, render a tab bar at the top.
    std::vector<MaterialDoc*> openDocs;
    for (auto& d : m_docs) if (d.open) openDocs.push_back(&d);

    if (openDocs.empty()) {
        // Shell is visible but no docs — show placeholder
        float sx, sy, sw, sh;
        if (getShellClientRect(sx, sy, sw, sh)) {
            ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoSavedSettings;
            if (ImGui::Begin("##MatEdShell", nullptr, flags)) {
                ImGui::SetCursorPosY(sh * 0.45f);
                float tw = ImGui::CalcTextSize("No material open.").x;
                ImGui::SetCursorPosX((sw - tw) * 0.5f);
                ImGui::TextDisabled("No material open.");
            }
            ImGui::End();
        }
        return;
    }

    if (openDocs.size() == 1) {
        renderDoc(*openDocs[0]);
        return;
    }

    // Multiple docs: tab bar rendered at top of the shell area
    float sx, sy, sw, sh;
    if (!getShellClientRect(sx, sy, sw, sh)) return;

    ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##MatEdShell", nullptr, flags)) { ImGui::End(); return; }

    if (ImGui::BeginTabBar("##MatTabs")) {
        for (auto* doc : openDocs) {
            std::string tabLabel = filenameOnly(doc->path);
            if (doc->dirty) tabLabel += " *";
            bool tabOpen = true;
            if (ImGui::BeginTabItem(tabLabel.c_str(), &tabOpen)) {
                if (!tabOpen) doc->open = false;
                doc->focused = true;

                // ── Inline content (same as renderDoc but without SetNextWindow) ──
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(doc->path.string().c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
                if (ImGui::SmallButton("Save")) {
                    if (saveToFile(doc->path, doc->data)) {
                        doc->dirty = false;
                        syncLinkedEntity(*doc);
                    }
                }
                ImGui::Separator();

                updatePreview(*doc);
                if (doc->previewTexture && doc->previewId != static_cast<ImTextureID>(0)) {
                    float available = ImGui::GetContentRegionAvail().x;
                    float psz = std::min(196.0f, available);
                    ImGui::SetCursorPosX((available - psz)*0.5f + ImGui::GetCursorPosX());
                    ImGui::Image(doc->previewId, ImVec2(psz, psz));
                    ImGui::Separator();
                }

                auto editFlag = [&](bool changed) { if (changed) markDirty(*doc); };
                ImGui::PushItemWidth(-1);
                editFlag(ImGui::ColorEdit4("Albedo",   glm::value_ptr(doc->data.albedoColor)));
                editFlag(ImGui::DragFloat("Roughness", &doc->data.roughness, 0.01f, 0.0f, 1.0f));
                editFlag(ImGui::DragFloat("Metallic",  &doc->data.metallic,  0.01f, 0.0f, 1.0f));
                ImGui::PopItemWidth();

                if (doc->dirty) doc->previewDirty = true;
                syncLinkedEntity(*doc);
                ImGui::EndTabItem();
            } else {
                doc->focused = false;
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();

    // Clean up closed docs
    m_docs.erase(std::remove_if(m_docs.begin(), m_docs.end(),
        [](const MaterialDoc& d) { return !d.open; }), m_docs.end());

    // Hide shell if all docs closed
    if (m_docs.empty()) showShell(false);
}

} // namespace Demon