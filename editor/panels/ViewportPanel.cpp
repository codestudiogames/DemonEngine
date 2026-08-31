// ==============================================================================
//  DemonEngine Editor::ViewportPanel  –  Implementation
// ==============================================================================
#include "ViewportPanel.h"
#include <ImGuizmo.h>
#include <commctrl.h>   // PROGRESS_CLASS, PBS_SMOOTH
#include <uxtheme.h>    // SetWindowTheme — disables visual styles on the bar
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <algorithm>
#include <cmath>
#include <limits>
#include "core/Application.h"
#include "input/Input.h"
#include "../EditorSettings.h"
#include "../widgets/DemonTheme.h"

#ifdef DEMON_USE_ASSIMP
#   include <assimp/Importer.hpp>
#   include <assimp/material.h>
#   include <assimp/postprocess.h>
#   include <assimp/scene.h>
#endif

namespace Demon {

namespace {
constexpr uint32_t kMinViewportDimension = 64;
constexpr uint32_t kMaxViewportDimension = 8192;

// Height of the solid black debug status bar pinned to the bottom of the
// viewport. The left debug column stops short of it.
constexpr float kDebugBarHeight = 46.0f;

int runtimeVirtualKey(int key)
{
    switch (key) {
        case Key::W: return 'W';
        case Key::A: return 'A';
        case Key::S: return 'S';
        case Key::D: return 'D';
        case Key::Space: return VK_SPACE;
        case Key::LeftShift: return VK_LSHIFT;
        default: return 0;
    }
}

bool runtimeKeyDown(int key)
{
    if (Input::isKeyDown(key))
        return true;
    const int vk = runtimeVirtualKey(key);
    return vk != 0 && ((GetAsyncKeyState(vk) & 0x8000) != 0);
}

// ── Win32 asset-loading progress window ───────────────────────────────────────
// Shows a modal Win32 dialog with a progress bar while an FBX/mesh is loading.
// Runs on the same thread as the caller — pumps messages to keep it responsive.

struct LoadProgressWnd {
    HWND     hwnd     = nullptr;
    HWND     bar      = nullptr;
    HWND     label    = nullptr;

    bool create(HINSTANCE hInst, const std::string& assetName)
    {
        const int W = 420, H = 110;
        RECT work{}; SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        int x = (work.right - work.left - W) / 2;
        int y = (work.bottom - work.top  - H) / 2;

        hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
                               "STATIC", "Loading Asset",
                               WS_POPUP | WS_CAPTION | WS_VISIBLE,
                               x, y, W, H,
                               nullptr, nullptr, hInst, nullptr);
        if (!hwnd) return false;
        // Dark background for the dialog window itself
        SetClassLongPtrA(hwnd, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(
                             CreateSolidBrush(RGB(18, 18, 18))));
        InvalidateRect(hwnd, nullptr, TRUE);

        // Label
        label = CreateWindowExA(0, "STATIC",
                                ("Loading: " + assetName).c_str(),
                                WS_CHILD | WS_VISIBLE | SS_LEFT,
                                12, 12, W - 24, 20,
                                hwnd, nullptr, hInst, nullptr);

        // Progress bar (0-100) — green, classic Win32 style (no Aero theme)
        bar = CreateWindowExA(0, PROGRESS_CLASSA, nullptr,
                              WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                              12, 40, W - 24, 22,
                              hwnd, nullptr, hInst, nullptr);
        // Strip the visual-styles theme so PBM_SETBARCOLOR takes effect.
        // With Aero/UxTheme active the bar ignores colour messages entirely.
        SetWindowTheme(bar, L"", L"");
        SendMessage(bar, PBM_SETRANGE,   0, MAKELPARAM(0, 100));
        SendMessage(bar, PBM_SETPOS,     0, 0);
        // Green bar, dark background
        SendMessage(bar, PBM_SETBARCOLOR, 0, static_cast<LPARAM>(RGB(34, 197, 94)));
        SendMessage(bar, PBM_SETBKCOLOR,  0, static_cast<LPARAM>(RGB(20, 20, 20)));

        // Sub-label
        CreateWindowExA(0, "STATIC",
                        "Processing geometry and uploading to GPU...",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        12, 70, W - 24, 18,
                        hwnd, nullptr, hInst, nullptr);

        UpdateWindow(hwnd);
        pump();
        return true;
    }

    void setProgress(int pct)
    {
        if (bar) SendMessage(bar, PBM_SETPOS, static_cast<WPARAM>(pct), 0);
        pump();
    }

    void destroy()
    {
        if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; bar = nullptr; label = nullptr; }
    }

    static void pump()
    {
        MSG msg{};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

#ifdef DEMON_USE_ASSIMP
glm::mat4 aiToGlmMatrix(const aiMatrix4x4& matrix)
{
    glm::mat4 result(1.0f);
    result[0][0] = matrix.a1; result[1][0] = matrix.a2; result[2][0] = matrix.a3; result[3][0] = matrix.a4;
    result[0][1] = matrix.b1; result[1][1] = matrix.b2; result[2][1] = matrix.b3; result[3][1] = matrix.b4;
    result[0][2] = matrix.c1; result[1][2] = matrix.c2; result[2][2] = matrix.c3; result[3][2] = matrix.c4;
    result[0][3] = matrix.d1; result[1][3] = matrix.d2; result[2][3] = matrix.d3; result[3][3] = matrix.d4;
    return result;
}

std::string pickImportName(const aiString& sourceName, std::string_view fallback)
{
    if (sourceName.length > 0 && sourceName.C_Str()[0] != '\0')
        return sourceName.C_Str();
    if (!fallback.empty())
        return std::string(fallback);
    return "Node";
}

// Extract an assimp embedded texture (as found in .glb / FBX-with-media) to a
// cache file on disk, and return that path so it flows through the normal
// file-based texture loader.  Assimp stores embedded textures either as the raw
// bytes of a compressed image (mHeight == 0, achFormatHint = "png"/"jpg"/…) or
// as uncompressed BGRA8888 texels (mHeight != 0).  Returns "" if extraction
// isn't possible.
std::string extractEmbeddedTexture(const aiScene& scene,
                                   const char* referenceName,
                                   const std::filesystem::path& modelPath)
{
    const aiTexture* embedded = scene.GetEmbeddedTexture(referenceName);
    if (!embedded)
        return {};

    // Cache directory: "<model-dir>/<model-stem>_embedded/"
    std::error_code ec;
    const std::filesystem::path cacheDir =
        modelPath.parent_path() / (modelPath.stem().string() + "_embedded");
    std::filesystem::create_directories(cacheDir, ec);

    // Build a stable, human-readable cache file name from the reference.
    std::string stem = referenceName ? referenceName : "tex";
    for (char& c : stem)
        if (c == '*' || c == '/' || c == '\\' || c == ':' || c == '.') c = '_';
    if (stem.empty()) stem = "tex";

    if (embedded->mHeight == 0) {
        // Compressed image blob — write bytes verbatim with the hinted extension.
        std::string ext = embedded->achFormatHint[0] ? embedded->achFormatHint : "png";
        const std::filesystem::path out = cacheDir / (stem + "." + ext);
        if (!std::filesystem::exists(out)) {
            std::ofstream file(out, std::ios::binary);
            if (!file)
                return {};
            file.write(reinterpret_cast<const char*>(embedded->pcData),
                       static_cast<std::streamsize>(embedded->mWidth));
        }
        return out.string();
    }

    // Uncompressed BGRA8888 texels — emit a 32bpp BMP (WIC-loadable) so the rest
    // of the pipeline can treat it like any other on-disk image.
    const uint32_t w = embedded->mWidth;
    const uint32_t h = embedded->mHeight;
    const std::filesystem::path out = cacheDir / (stem + ".bmp");
    if (!std::filesystem::exists(out)) {
        const uint32_t rowBytes = w * 4;
        const uint32_t pixelBytes = rowBytes * h;
        const uint32_t fileHeader = 14, infoHeader = 40;
        const uint32_t offset = fileHeader + infoHeader;
        const uint32_t fileSize = offset + pixelBytes;
        std::ofstream file(out, std::ios::binary);
        if (!file)
            return {};
        auto put32 = [&](uint32_t v){ file.put(char(v & 0xFF)); file.put(char((v>>8)&0xFF)); file.put(char((v>>16)&0xFF)); file.put(char((v>>24)&0xFF)); };
        auto put16 = [&](uint16_t v){ file.put(char(v & 0xFF)); file.put(char((v>>8)&0xFF)); };
        file.put('B'); file.put('M'); put32(fileSize); put16(0); put16(0); put32(offset);
        put32(infoHeader); put32(w); put32(h); put16(1); put16(32);
        put32(0); put32(pixelBytes); put32(2835); put32(2835); put32(0); put32(0);
        // aiTexel is B,G,R,A already; BMP bottom-up rows.
        for (int32_t y = static_cast<int32_t>(h) - 1; y >= 0; --y) {
            const aiTexel* row = embedded->pcData + static_cast<size_t>(y) * w;
            for (uint32_t x = 0; x < w; ++x) {
                file.put(char(row[x].b)); file.put(char(row[x].g));
                file.put(char(row[x].r)); file.put(char(row[x].a));
            }
        }
    }
    return out.string();
}

std::string resolveAssimpTexturePath(const aiScene& scene,
                                     const aiMaterial* material,
                                     aiTextureType type,
                                     const std::filesystem::path& modelPath)
{
    if (!material || material->GetTextureCount(type) == 0)
        return {};

    aiString textureName;
    if (material->GetTexture(type, 0, &textureName) != AI_SUCCESS || textureName.length == 0)
        return {};

    std::filesystem::path rawPath(textureName.C_Str());
    if (rawPath.empty())
        return {};

    // Embedded texture reference (assimp names these "*0", "*1", … in .glb and
    // FBX-with-embedded-media, or by an internal filename). Extract to a cache
    // file and use that path.
    if (rawPath.string().starts_with("*") || scene.GetEmbeddedTexture(textureName.C_Str())) {
        std::string extracted = extractEmbeddedTexture(scene, textureName.C_Str(), modelPath);
        if (!extracted.empty())
            return extracted;
        // fall through to on-disk resolution if extraction failed
        if (rawPath.string().starts_with("*"))
            return {};
    }

    if (rawPath.is_absolute() && std::filesystem::exists(rawPath))
        return rawPath.string();

    const std::filesystem::path modelDir = modelPath.parent_path();
    const std::filesystem::path fileName = rawPath.filename();
    const std::array<std::filesystem::path, 6> candidates = {
        modelDir / rawPath,
        modelDir / fileName,
        modelDir / "textures" / rawPath,
        modelDir / "textures" / fileName,
        modelDir / "Textures" / rawPath,
        modelDir / "Textures" / fileName
    };

    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate))
            return candidate.string();
    }

    return rawPath.string();
}

void attachAssimpMaterial(Scene& scene,
                          Entity entity,
                          const aiScene& importedScene,
                          unsigned int meshIndex,
                          const std::filesystem::path& modelPath)
{
    if (!entity || meshIndex >= importedScene.mNumMeshes)
        return;

    const aiMesh* mesh = importedScene.mMeshes[meshIndex];
    if (!mesh || mesh->mMaterialIndex >= importedScene.mNumMaterials)
        return;

    const aiMaterial* sourceMaterial = importedScene.mMaterials[mesh->mMaterialIndex];
    if (!sourceMaterial)
        return;

    auto& material = entity.hasComponent<MaterialComponent>()
        ? entity.getComponent<MaterialComponent>()
        : entity.addComponent<MaterialComponent>();

    material.materialPath = std::format("{}#assimp_material_{}", modelPath.string(), mesh->mMaterialIndex);
    material.materialLoaded = true;
    material.dirty = true;

    aiColor4D diffuse{};
    if (aiGetMaterialColor(sourceMaterial, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == AI_SUCCESS)
        material.albedoColor = {diffuse.r, diffuse.g, diffuse.b, diffuse.a};

    aiColor4D emissive{};
    if (aiGetMaterialColor(sourceMaterial, AI_MATKEY_COLOR_EMISSIVE, &emissive) == AI_SUCCESS) {
        material.emissiveColor = {emissive.r, emissive.g, emissive.b};
        material.emissiveStrength = std::max({emissive.r, emissive.g, emissive.b});
    }

    float opacity = 1.0f;
    if (sourceMaterial->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        material.albedoColor.a = std::clamp(opacity, 0.0f, 1.0f);
        material.alphaBlend = opacity < 0.999f;
    }

    float shininess = 32.0f;
    if (sourceMaterial->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
        material.roughness = std::clamp(1.0f - std::sqrt(std::clamp(shininess / 256.0f, 0.0f, 1.0f)), 0.06f, 1.0f);

    material.albedoTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_DIFFUSE, modelPath);
    if (material.albedoTexture.empty())  // glTF metallic-roughness base color
        material.albedoTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_BASE_COLOR, modelPath);
    material.normalTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_NORMALS, modelPath);
    if (material.normalTexture.empty())
        material.normalTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_HEIGHT, modelPath);
    material.metallicTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_METALNESS, modelPath);
    if (material.metallicTexture.empty())  // glTF packs metallic-roughness together
        material.metallicTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_DIFFUSE_ROUGHNESS, modelPath);
    material.emissiveTexture = resolveAssimpTexturePath(importedScene, sourceMaterial, aiTextureType_EMISSIVE, modelPath);
}

void importAssimpNodeRecursive(Scene& scene,
                               const aiScene& importedScene,
                               aiNode* node,
                               const std::filesystem::path& modelPath,
                               EntityID parentId)
{
    if (!node)
        return;

    Entity nodeEntity = scene.createEntity(pickImportName(node->mName, "Node"));
    if (parentId != NULL_ENTITY)
        scene.setParent(nodeEntity.getID(), parentId);
    auto& nodeTransform = nodeEntity.addComponent<TransformComponent>();
    nodeTransform.setFromMatrix(aiToGlmMatrix(node->mTransformation));

    if (node->mNumMeshes == 1) {
        auto& meshRenderer = nodeEntity.addComponent<MeshRendererComponent>();
        meshRenderer.meshPath = modelPath.string();
        meshRenderer.subMeshIndex = static_cast<int32_t>(node->mMeshes[0]);
        meshRenderer.preserveHierarchy = true;
        attachAssimpMaterial(scene, nodeEntity, importedScene, node->mMeshes[0], modelPath);
    } else if (node->mNumMeshes > 1) {
        for (unsigned int meshSlot = 0; meshSlot < node->mNumMeshes; ++meshSlot) {
            const unsigned int meshIndex = node->mMeshes[meshSlot];
            const aiMesh* mesh = importedScene.mMeshes[meshIndex];
            std::string meshName = pickImportName(mesh ? mesh->mName : aiString{}, std::format("Mesh {}", meshSlot + 1));

            Entity meshEntity = scene.createEntity(meshName);
            meshEntity.addComponent<TransformComponent>();
            auto& meshRenderer = meshEntity.addComponent<MeshRendererComponent>();
            meshRenderer.meshPath = modelPath.string();
            meshRenderer.subMeshIndex = static_cast<int32_t>(meshIndex);
            meshRenderer.preserveHierarchy = true;
            attachAssimpMaterial(scene, meshEntity, importedScene, meshIndex, modelPath);
            scene.setParent(meshEntity.getID(), nodeEntity.getID());
        }
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
        importAssimpNodeRecursive(scene, importedScene, node->mChildren[childIndex], modelPath, nodeEntity.getID());
}

Entity importModelHierarchy(Scene& scene, const std::filesystem::path& modelPath)
{
    Assimp::Importer importer;
    const aiScene* importedScene = importer.ReadFile(modelPath.string(),
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_ImproveCacheLocality |
        aiProcess_FlipUVs |
        aiProcess_FixInfacingNormals);

    if (!importedScene || !importedScene->mRootNode || !importedScene->HasMeshes()) {
        DEMON_LOG_ERROR("ViewportPanel: hierarchy import failed for '{}': {}",
                        modelPath.string(),
                        importer.GetErrorString());
        return {};
    }

    Entity rootEntity = scene.createEntity(modelPath.stem().string());
    rootEntity.addComponent<TransformComponent>();
    importAssimpNodeRecursive(scene, *importedScene, importedScene->mRootNode, modelPath, rootEntity.getID());
    return rootEntity;
}
#endif

std::shared_ptr<Mesh> findAnimatedMeshInHierarchy(Scene& scene, EntityID id)
{
    std::shared_ptr<Mesh> mesh = scene.getResolvedMesh(id);
    if (mesh && mesh->hasSkeleton() && mesh->hasAnimations())
        return mesh;

    for (EntityID child : scene.getChildren(id)) {
        if (std::shared_ptr<Mesh> childMesh = findAnimatedMeshInHierarchy(scene, child))
            return childMesh;
    }

    return {};
}

void attachAnimatorIfAnimated(Scene& scene, Entity entity)
{
    if (!entity)
        return;

    std::shared_ptr<Mesh> mesh = findAnimatedMeshInHierarchy(scene, entity.getID());
    if (!mesh || !mesh->hasSkeleton() || !mesh->hasAnimations())
        return;

    AnimatorComponent* animator = scene.getComponent<AnimatorComponent>(entity.getID());
    if (!animator)
        animator = &entity.addComponent<AnimatorComponent>();

    if (animator->currentClip.empty())
        animator->currentClip = mesh->getAnimationClips().front().name;

    animator->currentTime = 0.0f;
    animator->nextTime = 0.0f;
    animator->blendElapsed = 0.0f;
    animator->nextClip.clear();
}
} // namespace

template<typename T = ImTextureID>
static std::enable_if_t<std::is_pointer_v<T>, ImTextureID> toImTextureID(uint64_t handle)
{
    return reinterpret_cast<ImTextureID>(handle);
}

template<typename T = ImTextureID>
static std::enable_if_t<!std::is_pointer_v<T>, ImTextureID> toImTextureID(uint64_t handle)
{
    return static_cast<ImTextureID>(handle);
}

ViewportPanel::ViewportPanel() {
    m_editorCamera.setPerspective(60.0f, 16.0f / 9.0f, 0.01f, 1000.0f);
    m_editorCamera.setDistance(8.0f);
}

void ViewportPanel::setPlayMode(bool playing)
{
    if (m_playMode == playing)
        return;

    m_playMode = playing;
    m_playPaused = false;
    m_runtimePointerPrimed = false;
    m_runtimeVerticalVelocity = 0.0f;
    m_runtimeLookSmoothed = {0.0f, 0.0f};
    m_runtimeYaw = 0.0f;
    m_runtimePitch = 0.0f;
    m_runtimeGrounded = false;
    m_runtimeJumpWasDown = false;
    m_rightMouseHeld = false;
    m_activeTab = playing ? ViewportTab::Game : ViewportTab::Scene;
    ClipCursor(nullptr);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void ViewportPanel::setFullscreen(bool enabled)
{
    if (m_isFullscreen == enabled)
        return;

    m_isFullscreen = enabled;
    // Re-focusing the overlay window is required when entering fullscreen (see
    // m_fullscreenFocusFrames); a couple of frames covers the window resize that
    // the borderless switch triggers.
    m_fullscreenFocusFrames = enabled ? 3 : 0;
    Application::get().getWindow().setBorderlessFullscreen(enabled);
    ClipCursor(nullptr);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void ViewportPanel::onUpdate(float dt, const std::shared_ptr<Scene>& scene) {
    m_dt = dt;

    // FPS tracking
    if (dt > 0.0f) {
        m_currentFps = 1.0f / dt;
        m_fpsAccum += m_currentFps;
        ++m_fpsFrameCount;
        if (m_fpsFrameCount >= 60) {
            m_avgFps = m_fpsAccum / static_cast<float>(m_fpsFrameCount);
            m_fpsAccum = 0.0f;
            m_fpsFrameCount = 0;
        }
    }

    // System info refresh every 1 second
    m_sysRefreshTimer += dt;
    if (m_sysRefreshTimer >= 1.0f) {
        m_sysRefreshTimer = 0.0f;

        // RAM
        MEMORYSTATUSEX memStatus{};
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus)) {
            m_ramTotalMB  = static_cast<float>(memStatus.ullTotalPhys) / (1024.0f * 1024.0f);
            m_ramUsedMB   = m_ramTotalMB - static_cast<float>(memStatus.ullAvailPhys) / (1024.0f * 1024.0f);
            m_ramPercent  = (m_ramUsedMB / m_ramTotalMB) * 100.0f;
        }

        // Engine process memory
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
            m_engineMemMB    = static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
            m_otherAppsMemMB = std::max(0.0f, m_ramUsedMB - m_engineMemMB);
        }

        // VRAM via DXGI
        if (m_vramTotalMB <= 0.0f || m_gpuName.empty()) {
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
                IDXGIAdapter1* adapter = nullptr;
                if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
                    DXGI_ADAPTER_DESC1 desc{};
                    adapter->GetDesc1(&desc);
                    // GPU name
                    char gpuBuf[128]{};
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, gpuBuf, sizeof(gpuBuf), nullptr, nullptr);
                    m_gpuName = gpuBuf;
                    m_vramTotalMB = static_cast<float>(desc.DedicatedVideoMemory) / (1024.0f * 1024.0f);
                    adapter->Release();
                }
                factory->Release();
            }
        }

        // VRAM used via QueryVideoMemoryInfo (DXGI 1.4)
        IDXGIFactory4* factory4 = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory4)))) {
            IDXGIAdapter3* adapter3 = nullptr;
            IDXGIAdapter1* adapter1 = nullptr;
            if (SUCCEEDED(factory4->EnumAdapters1(0, &adapter1))) {
                if (SUCCEEDED(adapter1->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3)))) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
                    if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                        m_vramUsedMB  = static_cast<float>(info.CurrentUsage) / (1024.0f * 1024.0f);
                        if (m_vramTotalMB <= 0.0f)
                            m_vramTotalMB = static_cast<float>(info.Budget) / (1024.0f * 1024.0f);
                        m_vramPercent = (m_vramTotalMB > 0.0f) ? (m_vramUsedMB / m_vramTotalMB) * 100.0f : 0.0f;
                        if (m_vramPercent >= 90.0f && !m_vramWarningActive) {
                            DEMON_LOG_WARN("Video Memory Exceed: {:.0f} MB used of {:.0f} MB budget ({:.1f}%).",
                                           m_vramUsedMB,
                                           m_vramTotalMB,
                                           m_vramPercent);
                            m_vramWarningActive = true;
                        } else if (m_vramPercent < 80.0f) {
                            m_vramWarningActive = false;
                        }
                    }
                    adapter3->Release();
                }
                adapter1->Release();
            }
            factory4->Release();
        }
    }

    if (m_playMode) {
        handleRuntimeInput(dt, scene);
        return;
    }
    if (m_viewportFocused || m_viewportHovered)
        handleCameraInput(dt);
}

void ViewportPanel::handleRuntimeInput(float dt, const std::shared_ptr<Scene>& scene)
{
    if (!scene || (!m_viewportFocused && !m_viewportHovered)) {
        m_runtimePointerPrimed = false;
        ClipCursor(nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    const EntityID cameraId = scene->getPrimaryCameraID();
    if (cameraId == NULL_ENTITY) {
        m_runtimePointerPrimed = false;
        ClipCursor(nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    auto* cameraComponent = scene->getComponent<CameraComponent>(cameraId);
    if (!cameraComponent) {
        ClipCursor(nullptr);
        return;
    }

    TransformComponent* transform = scene->getComponent<TransformComponent>(cameraId);
    if (!transform) {
        auto cameraEntity = scene->getEntityByID(cameraId);
        transform = &cameraEntity.addComponent<TransformComponent>();
    }

    cameraComponent->camera.setFpsTransform(transform->translation,
                                            transform->rotation.y,
                                            transform->rotation.x);

    const auto& window = Application::get().getWindow();
    RECT clipRect{
        static_cast<LONG>(m_viewportBoundsMin.x),
        static_cast<LONG>(m_viewportBoundsMin.y),
        static_cast<LONG>(m_viewportBoundsMax.x),
        static_cast<LONG>(m_viewportBoundsMax.y)
    };
    POINT clipMin{clipRect.left, clipRect.top};
    POINT clipMax{clipRect.right, clipRect.bottom};
    ClientToScreen(window.getWin32Handle(), &clipMin);
    ClientToScreen(window.getWin32Handle(), &clipMax);
    clipRect = RECT{clipMin.x, clipMin.y, clipMax.x, clipMax.y};
    ClipCursor(&clipRect);

    POINT viewportCenter{
        static_cast<LONG>((m_viewportBoundsMin.x + m_viewportBoundsMax.x) * 0.5f),
        static_cast<LONG>((m_viewportBoundsMin.y + m_viewportBoundsMax.y) * 0.5f)
    };
    ClientToScreen(window.getWin32Handle(), &viewportCenter);

    if (!m_runtimePointerPrimed) {
        m_runtimeYaw = transform->rotation.y;
        m_runtimePitch = transform->rotation.x;
        m_runtimeLookSmoothed = {0.0f, 0.0f};
        cameraComponent->camera.setFpsTransform(transform->translation, m_runtimeYaw, m_runtimePitch);
        SetCursorPos(viewportCenter.x, viewportCenter.y);
        m_runtimePointerPrimed = true;
        SetCursor(nullptr);
        return;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    float dx = static_cast<float>(cursor.x - viewportCenter.x);
    float dy = static_cast<float>(cursor.y - viewportCenter.y);
    dx = std::clamp(dx, -48.0f, 48.0f);
    dy = std::clamp(dy, -48.0f, 48.0f);
    if (std::abs(dx) < 0.25f) dx = 0.0f;
    if (std::abs(dy) < 0.25f) dy = 0.0f;

    const float lookBlend = 1.0f - std::exp(-40.0f * std::max(dt, 0.0f));
    m_runtimeLookSmoothed = glm::mix(m_runtimeLookSmoothed, glm::vec2(dx, dy), lookBlend);

    m_runtimeYaw -= m_runtimeLookSmoothed.x * m_runtimeLookSensitivity;
    m_runtimePitch -= m_runtimeLookSmoothed.y * m_runtimeLookSensitivity;
    m_runtimePitch = std::clamp(m_runtimePitch, -88.0f, 88.0f);
    if (m_runtimeYaw > 360.0f || m_runtimeYaw < -360.0f)
        m_runtimeYaw = std::fmod(m_runtimeYaw, 360.0f);
    transform->rotation.x = m_runtimePitch;
    transform->rotation.y = m_runtimeYaw;
    transform->rotation.z = 0.0f;

    cameraComponent->camera.setFpsTransform(transform->translation, m_runtimeYaw, m_runtimePitch);

    float moveSpeed = m_runtimeMoveSpeed * dt;
    if (runtimeKeyDown(Key::LeftShift))
        moveSpeed *= 1.75f;

    glm::vec3 forward = cameraComponent->camera.getForward();
    glm::vec3 right = cameraComponent->camera.getRight();
    forward.y = 0.0f;
    right.y = 0.0f;
    forward = glm::length2(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    right = glm::length2(right) > 1e-6f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);

    glm::vec3 move(0.0f);
    if (runtimeKeyDown(Key::W)) move += forward;
    if (runtimeKeyDown(Key::S)) move -= forward;
    if (runtimeKeyDown(Key::D)) move += right;
    if (runtimeKeyDown(Key::A)) move -= right;

    if (glm::length(move) > 0.001f)
        transform->translation += glm::normalize(move) * moveSpeed;

    const bool jumpDown = runtimeKeyDown(Key::Space);
    if (m_runtimeGrounded && jumpDown && !m_runtimeJumpWasDown) {
        m_runtimeVerticalVelocity = m_runtimeJumpVelocity;
        m_runtimeGrounded = false;
    }
    m_runtimeJumpWasDown = jumpDown;

    m_runtimeVerticalVelocity -= m_runtimeGravity * std::max(dt, 0.0f);
    transform->translation.y += m_runtimeVerticalVelocity * dt;

    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    const float feetY = transform->translation.y - m_runtimeEyeHeight;
    const float groundY = scene->sampleGroundAtWorld(transform->translation.x,
                                                     transform->translation.z,
                                                     feetY + 2.0f,
                                                     200.0f,
                                                     &groundNormal);
    if (groundY > std::numeric_limits<float>::lowest()) {
        const float targetEyeY = groundY + m_runtimeEyeHeight;
        const bool closeEnoughToStand = transform->translation.y <= targetEyeY + 0.28f;
        const bool steppingWhileGrounded = m_runtimeGrounded && transform->translation.y <= targetEyeY + 0.65f;
        if ((closeEnoughToStand || steppingWhileGrounded) && m_runtimeVerticalVelocity <= 0.0f) {
            transform->translation.y = targetEyeY;
            m_runtimeVerticalVelocity = 0.0f;
            m_runtimeGrounded = true;
        } else {
            m_runtimeGrounded = false;
        }
    } else {
        m_runtimeGrounded = false;
    }

    cameraComponent->camera.setFpsTransform(transform->translation, m_runtimeYaw, m_runtimePitch);
    SetCursorPos(viewportCenter.x, viewportCenter.y);
    SetCursor(nullptr);
}

void ViewportPanel::handleCameraInput(float dt) {
    const float lookBlend = 1.0f - std::exp(-m_lookSmoothness * std::max(dt, 0.0f));

    if (m_gizmoCapturing) {
        m_rightMouseHeld = false;
        m_smoothedLookDelta = glm::mix(m_smoothedLookDelta, glm::vec2(0.0f), lookBlend);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    const bool wasRightMouseHeld = m_rightMouseHeld;
    m_rightMouseHeld = Input::isMouseButtonDown(MouseButton::Right);

    // Cursor: crosshair while rotating, arrow otherwise — never hidden
    if (m_rightMouseHeld && !wasRightMouseHeld)
        SetCursor(LoadCursor(nullptr, IDC_CROSS));
    else if (!m_rightMouseHeld && wasRightMouseHeld)
        SetCursor(LoadCursor(nullptr, IDC_ARROW));

    if (m_rightMouseHeld) {
        // Re-apply each frame — Win32 resets the cursor on every WM_SETCURSOR
        SetCursor(LoadCursor(nullptr, IDC_CROSS));

        auto [dx, dy] = Input::getMouseDelta();
        m_smoothedLookDelta = glm::mix(m_smoothedLookDelta, glm::vec2(dx, dy), lookBlend);
        // Invert Y: screen-down = look-down
        m_editorCamera.orbitAroundTarget(-m_smoothedLookDelta.x * m_sensitivity,
                                         -m_smoothedLookDelta.y * m_sensitivity);

        float speed = m_moveSpeed * dt;
        if (Input::isKeyDown(Key::LeftShift)) speed *= 4.0f;

        glm::vec3 move{0};
        if (Input::isKeyDown(Key::W)) move += m_editorCamera.getForward();
        if (Input::isKeyDown(Key::S)) move -= m_editorCamera.getForward();
        if (Input::isKeyDown(Key::D)) move += m_editorCamera.getRight();
        if (Input::isKeyDown(Key::A)) move -= m_editorCamera.getRight();
        if (Input::isKeyDown(Key::E)) move += glm::vec3(0, 1, 0);
        if (Input::isKeyDown(Key::Q)) move -= glm::vec3(0, 1, 0);

        if (glm::length(move) > 0.001f) {
            glm::vec3 delta  = glm::normalize(move) * speed;
            glm::vec3 focal  = m_editorCamera.getPosition()
                             + m_editorCamera.getForward() * m_editorCamera.getDistance();
            m_editorCamera.setFocalPoint(focal + delta);
        }
    } else {
        m_smoothedLookDelta = glm::mix(m_smoothedLookDelta, glm::vec2(0.0f), lookBlend);
    }

    // Scroll zoom
    float scroll = Input::getScrollDelta();
    if (std::abs(scroll) > 0.001f)
        m_editorCamera.zoom(scroll * m_scrollSensitivity);

    // Middle-mouse pan
    if (Input::isMouseButtonDown(MouseButton::Middle)) {
        auto [dx, dy] = Input::getMouseDelta();
        m_editorCamera.pan(dx * 0.01f, -dy * 0.01f);
    }

    // Gizmo shortcuts — only trigger on key press (not hold) to avoid conflicts
    // with WASD fly when right-mouse is held
    if (!m_rightMouseHeld) {
        if (Input::isKeyPressed(Key::Q)) m_gizmoMode = GizmoMode::None;
        if (Input::isKeyPressed(Key::W)) m_gizmoMode = GizmoMode::Translate;
        if (Input::isKeyPressed(Key::E)) m_gizmoMode = GizmoMode::Rotate;
        if (Input::isKeyPressed(Key::R)) m_gizmoMode = GizmoMode::Scale;
        if (Input::isKeyPressed(Key::X)) m_localSpace = !m_localSpace;
    }
}

void ViewportPanel::render(const std::shared_ptr<Scene>& scene) {
    // F11 fullscreen toggle
    if (ImGui::IsKeyPressed(ImGuiKey_F11))
        setFullscreen(!m_isFullscreen);

    const char* windowName = m_isFullscreen ? "Viewport##FullscreenOverlay" : "Viewport";
    ImGuiWindowFlags winFlags = editorPanelFlags();

    if (m_isFullscreen) {
        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos(mainViewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(mainViewport->Size, ImGuiCond_Always);
        // NOTE: deliberately NOT using ImGuiWindowFlags_NoBringToFrontOnFocus.
        // ImGui pushes a new window carrying that flag to the front of g.Windows,
        // which is the BOTTOM of the render order, so the dockspace host window
        // (and its opaque empty central node) would completely hide the viewport.
        winFlags |= ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoDocking |
                    ImGuiWindowFlags_NoSavedSettings;
        // The overlay window persists in g.Windows between fullscreen sessions, so
        // its stack position has to be re-asserted every time we enter fullscreen.
        if (m_fullscreenFocusFrames > 0) {
            --m_fullscreenFocusFrames;
            ImGui::SetNextWindowFocus();
        }
    }

    ImGui::Begin(windowName, nullptr, winFlags);

    if (m_playMode)
        m_activeTab = ViewportTab::Game;

    renderToolbar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::BeginChild("ViewportCanvas", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    m_gizmoCapturing = false;
    m_viewportFocused = ImGui::IsWindowFocused();
    m_viewportHovered = ImGui::IsWindowHovered();
    m_dropTargetHovered = false;

    // Resize detection
    auto size = ImGui::GetContentRegionAvail();
    if (std::isfinite(size.x) && std::isfinite(size.y) &&
        size.x >= static_cast<float>(kMinViewportDimension) &&
        size.y >= static_cast<float>(kMinViewportDimension))
    {
        const uint32_t w = static_cast<uint32_t>(std::clamp<int>(
            static_cast<int>(size.x), static_cast<int>(kMinViewportDimension), static_cast<int>(kMaxViewportDimension)));
        const uint32_t h = static_cast<uint32_t>(std::clamp<int>(
            static_cast<int>(size.y), static_cast<int>(kMinViewportDimension), static_cast<int>(kMaxViewportDimension)));

        if (w != m_width || h != m_height) {
            DEMON_LOG_INFO("ViewportPanel: resize request {}x{} -> {}x{}", m_width, m_height, w, h);
            m_width  = w;
            m_height = h;
            m_resizeCooldownFrames = 6;
            m_editorCamera.setViewportSize(w, h);
            if (scene)
                scene->onViewportResize(w, h);
            Application::get().getRenderer().resizeViewport(w, h);
        }
    }

    const auto viewportHandle = Application::get().getRenderer().getViewportDescriptor();
    m_framebufferTexID = toImTextureID(viewportHandle.ptr);

    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    if (m_framebufferTexID) {
        ImGui::Image(m_framebufferTexID, size);
    } else {
        ImGui::Dummy(size);
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(viewportPos,
                          {viewportPos.x + size.x, viewportPos.y + size.y},
                          IM_COL32(20, 20, 30, 255));
        const char* msg = "DX12 Viewport Initialising...";
        ImVec2 textSize = ImGui::CalcTextSize(msg);
        ImVec2 textPos  = { viewportPos.x + (size.x - textSize.x) * 0.5f,
                            viewportPos.y + (size.y - textSize.y) * 0.5f };
        dl->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    }
    m_viewportBoundsMin = ImGui::GetItemRectMin();
    m_viewportBoundsMax = ImGui::GetItemRectMax();
    viewportPos = m_viewportBoundsMin;
    size = { m_viewportBoundsMax.x - m_viewportBoundsMin.x,
             m_viewportBoundsMax.y - m_viewportBoundsMin.y };

    // Draw primary camera frustum in scene view
    const bool overlaysStable = (m_resizeCooldownFrames <= 0);
    if (m_resizeCooldownFrames > 0)
        --m_resizeCooldownFrames;

    const bool sceneViewActive = (m_activeTab == ViewportTab::Scene) && !m_playMode;

    if (sceneViewActive && overlaysStable &&
        std::isfinite(size.x) && std::isfinite(size.y) && size.x > 1.0f && size.y > 1.0f)
        drawCameraFrustum(scene, viewportPos, size);

    // Drag-drop mesh files from Content Browser
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* preview = ImGui::GetDragDropPayload();
            preview && preview->IsDataType("CONTENT_BROWSER_ITEM"))
        {
            m_dropTargetHovered = true;
        }

        if (scene) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
            const char* path = static_cast<const char*>(payload->Data);
            if (path) {
                std::filesystem::path p(path);
                auto ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae") {
                    auto absPath = std::filesystem::absolute(p);
                    if (std::filesystem::exists(absPath)) {
                        // ── Show progress window while loading ────────────────
                        LoadProgressWnd prog;
                        HINSTANCE hInst = GetModuleHandle(nullptr);
                        prog.create(hInst, p.filename().string());

                        prog.setProgress(10);   // starting

                        Entity importedEntity;

                        prog.setProgress(30);   // entity created

#ifdef DEMON_USE_ASSIMP
                        const bool supportsHierarchyImport =
                            (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae");
                        if (supportsHierarchyImport)
                            importedEntity = importModelHierarchy(*scene, absPath);
#endif

                        if (!importedEntity) {
                            importedEntity = scene->createEntity(p.stem().string());
                            importedEntity.addComponent<TransformComponent>();
                            auto& mr = importedEntity.addComponent<MeshRendererComponent>();
                            mr.meshPath = absPath.string();
                            mr.preserveHierarchy = (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae");
                        }

                        prog.setProgress(60);   // path assigned, mesh queued

                        attachAnimatorIfAnimated(*scene, importedEntity);

                        m_selected    = importedEntity;
                        m_sceneEdited = true;

                        prog.setProgress(90);   // scene marked dirty

                        // Small yield so the GPU upload (next beginFrame) can start
                        Sleep(80);
                        prog.setProgress(100);
                        Sleep(120);             // let user see 100%
                        prog.destroy();
                        // ─────────────────────────────────────────────────────
                    } else {
                        DEMON_LOG_ERROR("Dropped mesh missing on disk: '{}'", absPath.string());
                    }
                }
            }
        }
        }
        ImGui::EndDragDropTarget();
    }

    const bool canDrawViewportOverlays = size.x > (m_viewGizmoSize + 32.0f) && size.y > (m_viewGizmoSize + 32.0f);
    if (overlaysStable) {
        // The debug view already reports mode/resolution in its left column, so the
        // rounded badges would just collide with it.
        if (!m_debugModeEnabled)
            renderOverlayStats();

        // Update hovered entity (crosshair center pick in play mode, mouse pick in scene mode)
        if (m_debugModeEnabled) {
            if (m_playMode)
                m_hoveredEntity = pickEntityAtCenter(scene);
            else if (m_viewportHovered)
                m_hoveredEntity = pickEntityAtCursor(scene);
            else
                m_hoveredEntity = NULL_ENTITY;
        }

        if (m_dropTargetHovered) {
            auto* dl = ImGui::GetWindowDrawList();
            dl->AddRect(m_viewportBoundsMin, m_viewportBoundsMax, IM_COL32(210, 72, 58, 220), 8.0f, 0, 2.0f);
            const char* label = "Drop model to add it to the scene";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 textPos{
                m_viewportBoundsMin.x + (size.x - textSize.x) * 0.5f,
                m_viewportBoundsMin.y + 18.0f
            };
            dl->AddRectFilled({textPos.x - 10.0f, textPos.y - 6.0f},
                              {textPos.x + textSize.x + 10.0f, textPos.y + textSize.y + 6.0f},
                              IM_COL32(22, 22, 24, 220),
                              6.0f);
            dl->AddText(textPos, IM_COL32(236, 236, 236, 255), label);
        }

        if (sceneViewActive) {
            drawSelectionOverlay(scene, viewportPos, size);
        }

        if (sceneViewActive && canDrawViewportOverlays) {
            renderGizmo();
            renderViewGizmo();
            m_gizmoCapturing = ImGuizmo::IsOver() || ImGuizmo::IsUsing() || ImGuizmo::IsUsingViewManipulate();
        } else if (m_playMode) {
            renderRuntimeCrosshair();
            m_gizmoCapturing = false;
        } else {
            renderGameViewOverlay();
            m_gizmoCapturing = false;
        }
    }

    if (sceneViewActive &&
        m_viewportHovered &&
        ImGui::IsMouseHoveringRect(m_viewportBoundsMin, m_viewportBoundsMax, false) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGuizmo::IsOver() &&
        !ImGuizmo::IsUsing())
    {
        m_selectionRequest = pickEntityAtCursor(scene);
        m_selectionRequestPending = true;
    }

    // Debug overlays
    if (m_debugModeEnabled) {
        renderDebugBar(scene);
        renderHoverObjectInfo(scene);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

void ViewportPanel::renderGizmo() {
    if (!m_selected || m_gizmoMode == GizmoMode::None) return;
    if (!m_selected.hasComponent<TransformComponent>()) return;
    const float viewportWidth = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float viewportHeight = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (viewportWidth <= 1.0f || viewportHeight <= 1.0f)
        return;

    auto& tc = m_selected.getComponent<TransformComponent>();

    ImGuizmo::SetOrthographic(m_editorCamera.getProjectionType() == ProjectionType::Orthographic);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(m_viewportBoundsMin.x, m_viewportBoundsMin.y, viewportWidth, viewportHeight);

    glm::mat4 view = m_editorCamera.getViewMatrix();
    glm::mat4 proj = m_editorCamera.getProjectionMatrix();

    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    switch (m_gizmoMode) {
        case GizmoMode::Translate: op = ImGuizmo::TRANSLATE; break;
        case GizmoMode::Rotate:    op = ImGuizmo::ROTATE;    break;
        case GizmoMode::Scale:     op = ImGuizmo::SCALE;     break;
        default: break;
    }
    ImGuizmo::MODE mode = m_localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    glm::mat4 transform = tc.getMatrix();

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                             op, mode, glm::value_ptr(transform), nullptr, nullptr))
    {
        tc.setFromMatrix(transform);
        m_sceneEdited = true;
    }

    ImGuizmo::DrawGrid(glm::value_ptr(view), glm::value_ptr(proj),
                       glm::value_ptr(glm::mat4(1.0f)), 20.0f);
}

void ViewportPanel::renderToolbar() {
    const auto& toolbarStats = Application::get().getRenderer().getStats();

    auto toolbarAccentButton = [](bool active) {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.18f, 0.16f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.21f, 0.18f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.74f, 0.21f, 0.18f, 1.00f));
        }
    };
    auto toolbarAccentButtonEnd = [](bool active) {
        if (active)
            ImGui::PopStyleColor(3);
    };
    auto segmentedButton = [&](const char* label, bool active, bool enabled = true) {
        ImGui::BeginDisabled(!enabled);
        toolbarAccentButton(active);
        const bool pressed = ImGui::Button(label, ImVec2(74.0f, 0.0f));
        toolbarAccentButtonEnd(active);
        ImGui::EndDisabled();
        return pressed && enabled;
    };
    auto toolButton = [&](const char* label, GizmoMode mode, const char* tooltip) {
        const bool active = (m_gizmoMode == mode);
        toolbarAccentButton(active);
        if (ImGui::Button(label, ImVec2(72.0f, 0.0f)))
            m_gizmoMode = mode;
        toolbarAccentButtonEnd(active);
        if (ImGui::IsItemHovered() && tooltip)
            ImGui::SetTooltip("%s", tooltip);
    };

    if (ImGui::BeginTable("ViewportToolbar", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Tabs", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("Transport", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Tools", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (segmentedButton("Scene", m_activeTab == ViewportTab::Scene, !m_playMode))
            m_activeTab = ViewportTab::Scene;
        ImGui::SameLine();
        if (segmentedButton("Game", m_activeTab == ViewportTab::Game))
            m_activeTab = ViewportTab::Game;
        ImGui::SameLine();
        ImGui::TextDisabled(m_activeTab == ViewportTab::Scene ? "Editor View" : "Runtime View");

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Play", ImVec2(72.0f, 0.0f)) && !m_playMode)
            m_togglePlayRequested = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enter runtime mode");
        ImGui::SameLine();

        ImGui::BeginDisabled(!m_playMode);
        toolbarAccentButton(m_playPaused);
        if (ImGui::Button(m_playPaused ? "Resume" : "Pause", ImVec2(84.0f, 0.0f)))
            m_pauseToggleRequested = true;
        toolbarAccentButtonEnd(m_playPaused);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && m_playMode)
            ImGui::SetTooltip("Pause or resume the runtime simulation");
        ImGui::SameLine();

        ImGui::BeginDisabled(!m_playMode);
        if (ImGui::Button("Stop", ImVec2(72.0f, 0.0f)))
            m_togglePlayRequested = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && m_playMode)
            ImGui::SetTooltip("Exit runtime mode");

        ImGui::TableSetColumnIndex(2);
        if (!m_playMode) {
            toolButton("Select", GizmoMode::None, "Selection only [Q]");
            ImGui::SameLine();
            toolButton("Move", GizmoMode::Translate, "Translate gizmo [W]");
            ImGui::SameLine();
            toolButton("Rotate", GizmoMode::Rotate, "Rotate gizmo [E]");
            ImGui::SameLine();
            toolButton("Scale", GizmoMode::Scale, "Scale gizmo [R]");
            ImGui::SameLine();
            const bool wasLocalSpace = m_localSpace;
            toolbarAccentButton(wasLocalSpace);
            if (ImGui::Button(wasLocalSpace ? "Local" : "Global", ImVec2(78.0f, 0.0f)))
                m_localSpace = !m_localSpace;
            toolbarAccentButtonEnd(wasLocalSpace);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Toggle transform space [X]");
            ImGui::SameLine(0, 16);
            // Fullscreen toggle
            // Capture state BEFORE the button so Push/Pop counts always match,
            // even when the button click flips m_isFullscreen mid-frame.
            const bool wasFullscreen = m_isFullscreen;
            toolbarAccentButton(wasFullscreen);
            if (ImGui::Button(wasFullscreen ? "[  ]" : "[ ]", ImVec2(42.0f, 0.0f)))
                setFullscreen(!wasFullscreen);
            toolbarAccentButtonEnd(wasFullscreen);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(m_isFullscreen ? "Exit fullscreen viewport" : "Fullscreen viewport [F11]");
        } else {
            ImGui::TextDisabled(m_playPaused ? "Runtime paused" : "Runtime live");
            ImGui::SameLine();
            ImGui::TextDisabled("WASD + Mouse Look");
        }

        ImGui::EndTable();
    }

    if (toolbarStats.culledCount > 0)
        ImGui::TextDisabled("Culled this frame: %u", toolbarStats.culledCount);
    else
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

    ImGui::Separator();
    return;
    ImGui::SetCursorPos({8, 28});
    ImGui::BeginGroup();

    if (ImGui::SmallButton(m_playMode ? "Stop" : "Play"))
        m_togglePlayRequested = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m_playMode ? "Exit runtime mode" : "Enter runtime mode");

    if (!m_playMode)
        ImGui::SameLine();

    auto btn = [&](const char* label, GizmoMode mode, const char* shortcut) {
        bool active = (m_gizmoMode == mode);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        if (ImGui::SmallButton(label)) m_gizmoMode = mode;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  [%s]", label, shortcut);
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };

    if (!m_playMode) {
        btn("  ⊕  ", GizmoMode::None,      "Q");
        btn("  ↔  ", GizmoMode::Translate, "W");
        btn("  ↻  ", GizmoMode::Rotate,    "E");
        btn("  ⤢  ", GizmoMode::Scale,     "R");
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled(m_localSpace ? "[Local]" : "[World]");
        if (ImGui::IsItemClicked()) m_localSpace = !m_localSpace;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Local/World [X]");
    } else {
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("[Runtime WASD + Mouse Look]");
    }

    // Show cull stats when available
    const auto& stats = Application::get().getRenderer().getStats();
    if (stats.culledCount > 0) {
        ImGui::SameLine(0, 20);
        ImGui::TextDisabled("Culled: %u", stats.culledCount);
    }

    ImGui::EndGroup();
}

void ViewportPanel::renderOverlayStats()
{
    const float width = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float height = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (width <= 40.0f || height <= 40.0f)
        return;

    auto* drawList = ImGui::GetWindowDrawList();
    auto drawBadge = [&](ImVec2 pos, const std::string& text, ImU32 bg, ImU32 fg = IM_COL32(238, 238, 238, 255)) {
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 max{pos.x + textSize.x + 18.0f, pos.y + textSize.y + 10.0f};
        drawList->AddRectFilled(pos, max, bg, 6.0f);
        drawList->AddText({pos.x + 9.0f, pos.y + 5.0f}, fg, text.c_str());
    };

    const std::string modeLabel = m_playMode
        ? (m_playPaused ? "Game View  |  Paused" : "Game View  |  Live")
        : (m_activeTab == ViewportTab::Scene ? "Scene View  |  Editor Camera" : "Game View  |  Preview");
    drawBadge({m_viewportBoundsMin.x + 12.0f, m_viewportBoundsMin.y + 12.0f},
              modeLabel,
              IM_COL32(24, 24, 26, 220));

    const std::string resolutionLabel = std::format("{} x {}", m_width, m_height);
    drawBadge({m_viewportBoundsMin.x + 12.0f, m_viewportBoundsMin.y + 46.0f},
              resolutionLabel,
              IM_COL32(18, 18, 20, 210),
              IM_COL32(170, 176, 184, 255));

    if (!m_playMode && m_activeTab == ViewportTab::Scene) {
        drawBadge({m_viewportBoundsMin.x + 12.0f, m_viewportBoundsMax.y - 40.0f},
                  "RMB Look  |  MMB Pan  |  Wheel Zoom  |  W/E/R Gizmo",
                  IM_COL32(18, 18, 20, 210),
                  IM_COL32(188, 192, 198, 255));
    }
}

void ViewportPanel::renderGameViewOverlay() const
{
    const float width = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float height = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (width <= 120.0f || height <= 120.0f)
        return;

    const char* title = "Game preview";
    const char* body = "Press Play to switch from editor camera to the runtime view.";
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const ImVec2 bodySize = ImGui::CalcTextSize(body);

    const ImVec2 min{
        m_viewportBoundsMin.x + (width - std::max(titleSize.x, bodySize.x) - 48.0f) * 0.5f,
        m_viewportBoundsMin.y + (height - 76.0f) * 0.5f
    };
    const ImVec2 max{
        min.x + std::max(titleSize.x, bodySize.x) + 48.0f,
        min.y + 76.0f
    };

    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, IM_COL32(18, 18, 20, 225), 8.0f);
    drawList->AddRect(min, max, IM_COL32(210, 72, 58, 120), 8.0f, 0, 1.5f);
    drawList->AddText({min.x + 18.0f, min.y + 14.0f}, IM_COL32(238, 238, 238, 255), title);
    drawList->AddText({min.x + 18.0f, min.y + 42.0f}, IM_COL32(176, 180, 186, 255), body);
}

bool ViewportPanel::resolveSelectedBounds(const std::shared_ptr<Scene>& scene,
                                          glm::mat4& outTransform,
                                          glm::vec3& outBoundsMin,
                                          glm::vec3& outBoundsMax,
                                          std::string* outLabel) const
{
    if (!scene || !m_selected)
        return false;

    const EntityID entityId = m_selected.getID();
    outTransform = scene->getWorldTransform(entityId);

    if (outLabel) {
        if (const auto* tag = scene->getComponent<TagComponent>(entityId); tag && !tag->tag.empty())
            *outLabel = tag->tag;
        else
            *outLabel = std::format("Entity {}", entityId);
    }

    if (const auto* meshRenderer = scene->getComponent<MeshRendererComponent>(entityId)) {
        (void)meshRenderer;
        if (std::shared_ptr<Mesh> mesh = scene->getResolvedMesh(entityId)) {
            outBoundsMin = mesh->getBoundsMin();
            outBoundsMax = mesh->getBoundsMax();
            return true;
        }
    }

    if (const auto* collider = scene->getComponent<BoxColliderComponent>(entityId)) {
        outTransform *= glm::translate(glm::mat4(1.0f), collider->offset);
        outBoundsMin = -collider->halfExtents;
        outBoundsMax = collider->halfExtents;
        return true;
    }

    if (const auto* terrain = scene->getComponent<TerrainComponent>(entityId)) {
        outBoundsMin = {-terrain->sizeX * 0.5f, 0.0f, -terrain->sizeZ * 0.5f};
        outBoundsMax = {terrain->sizeX * 0.5f, std::max(terrain->maxHeight, 0.1f), terrain->sizeZ * 0.5f};
        return true;
    }

    if (const auto* water = scene->getComponent<WaterBodyComponent>(entityId)) {
        outBoundsMin = {-water->size.x * 0.5f, -std::max(water->depth, 0.1f), -water->size.y * 0.5f};
        outBoundsMax = {water->size.x * 0.5f, 0.12f, water->size.y * 0.5f};
        return true;
    }

    if (scene->getComponent<TransformComponent>(entityId)) {
        outBoundsMin = {-0.5f, -0.5f, -0.5f};
        outBoundsMax = {0.5f, 0.5f, 0.5f};
        return true;
    }

    return false;
}

void ViewportPanel::drawSelectionOverlay(const std::shared_ptr<Scene>& scene,
                                         const ImVec2& viewportPos,
                                         const ImVec2& size) const
{
    if (!scene || !m_selected)
        return;

    glm::mat4 transform(1.0f);
    glm::vec3 boundsMin(0.0f);
    glm::vec3 boundsMax(0.0f);
    std::string label;
    if (!resolveSelectedBounds(scene, transform, boundsMin, boundsMax, &label))
        return;

    const std::array<glm::vec3, 8> corners = {{
        {boundsMin.x, boundsMin.y, boundsMin.z},
        {boundsMax.x, boundsMin.y, boundsMin.z},
        {boundsMax.x, boundsMax.y, boundsMin.z},
        {boundsMin.x, boundsMax.y, boundsMin.z},
        {boundsMin.x, boundsMin.y, boundsMax.z},
        {boundsMax.x, boundsMin.y, boundsMax.z},
        {boundsMax.x, boundsMax.y, boundsMax.z},
        {boundsMin.x, boundsMax.y, boundsMax.z}
    }};

    glm::mat4 viewProjection = m_editorCamera.getProjectionMatrix() * m_editorCamera.getViewMatrix();
    std::array<ImVec2, 8> screenCorners{};
    std::array<bool, 8> validCorners{};
    ImVec2 screenMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    ImVec2 screenMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    size_t validCount = 0;

    auto project = [&](const glm::vec3& point, ImVec2& out) -> bool {
        glm::vec4 clip = viewProjection * transform * glm::vec4(point, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w))
            return false;
        if (clip.w <= 0.0001f)
            return false;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        out.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * size.x;
        out.y = viewportPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y;
        return std::isfinite(out.x) && std::isfinite(out.y);
    };

    for (size_t i = 0; i < corners.size(); ++i) {
        validCorners[i] = project(corners[i], screenCorners[i]);
        if (!validCorners[i])
            continue;
        ++validCount;
        screenMin.x = std::min(screenMin.x, screenCorners[i].x);
        screenMin.y = std::min(screenMin.y, screenCorners[i].y);
        screenMax.x = std::max(screenMax.x, screenCorners[i].x);
        screenMax.y = std::max(screenMax.y, screenCorners[i].y);
    }

    if (validCount < 2)
        return;

    auto* drawList = ImGui::GetWindowDrawList();
    const ImU32 glow = IM_COL32(229, 86, 69, 88);
    const ImU32 lineColor = IM_COL32(234, 98, 78, 255);
    const std::array<std::pair<int, int>, 12> edges = {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    }};

    for (const auto& [a, b] : edges) {
        if (!validCorners[a] || !validCorners[b])
            continue;
        drawList->AddLine(screenCorners[a], screenCorners[b], glow, 4.0f);
        drawList->AddLine(screenCorners[a], screenCorners[b], lineColor, 1.6f);
    }

    if (screenMin.x < screenMax.x && screenMin.y < screenMax.y) {
        drawList->AddRect({screenMin.x - 2.0f, screenMin.y - 2.0f},
                          {screenMax.x + 2.0f, screenMax.y + 2.0f},
                          glow,
                          6.0f,
                          0,
                          3.0f);
        drawList->AddRect(screenMin, screenMax, lineColor, 6.0f, 0, 1.4f);

        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const ImVec2 badgeMin{screenMin.x, std::max(m_viewportBoundsMin.y + 8.0f, screenMin.y - textSize.y - 14.0f)};
        const ImVec2 badgeMax{badgeMin.x + textSize.x + 18.0f, badgeMin.y + textSize.y + 10.0f};
        drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(20, 20, 22, 230), 6.0f);
        drawList->AddText({badgeMin.x + 9.0f, badgeMin.y + 5.0f}, IM_COL32(240, 240, 240, 255), label.c_str());
    }
}

void ViewportPanel::renderRuntimeCrosshair() const
{
    const float width = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float height = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (width <= 16.0f || height <= 16.0f)
        return;

    const ImVec2 center{
        (m_viewportBoundsMin.x + m_viewportBoundsMax.x) * 0.5f,
        (m_viewportBoundsMin.y + m_viewportBoundsMax.y) * 0.5f
    };

    auto* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(230, 230, 230, 220);
    drawList->AddLine({center.x - 8.0f, center.y}, {center.x + 8.0f, center.y}, color, 1.5f);
    drawList->AddLine({center.x, center.y - 8.0f}, {center.x, center.y + 8.0f}, color, 1.5f);
}

EntityID ViewportPanel::pickEntityAtCursor(const std::shared_ptr<Scene>& scene) const
{
    if (!scene)
        return NULL_ENTITY;

    const ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x < m_viewportBoundsMin.x || mouse.x > m_viewportBoundsMax.x ||
        mouse.y < m_viewportBoundsMin.y || mouse.y > m_viewportBoundsMax.y)
    {
        return NULL_ENTITY;
    }

    const float width = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float height = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (width <= 1.0f || height <= 1.0f)
        return NULL_ENTITY;

    const float u = (mouse.x - m_viewportBoundsMin.x) / width;
    const float v = (mouse.y - m_viewportBoundsMin.y) / height;
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;

    auto [rayOrigin, rayDirection] = m_editorCamera.castRay(ndcX, ndcY);
    return const_cast<Scene&>(*scene).pickEntity(rayOrigin, rayDirection);
}

void ViewportPanel::renderViewGizmo() {
    ImVec2 sz{ m_viewGizmoSize, m_viewGizmoSize };
    const float viewportWidth = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float viewportHeight = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (viewportWidth <= sz.x + 20.0f || viewportHeight <= sz.y + 20.0f)
        return;

    ImGuizmo::SetOrthographic(m_editorCamera.getProjectionType() == ProjectionType::Orthographic);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(m_viewportBoundsMin.x, m_viewportBoundsMin.y, viewportWidth, viewportHeight);

    ImVec2 pos{
        m_viewportBoundsMax.x - sz.x - 12.0f,
        m_viewportBoundsMin.y + 12.0f
    };

    glm::mat4 view   = m_editorCamera.getViewMatrix();
    glm::mat4 before = view;

    ImGuizmo::ViewManipulate(glm::value_ptr(view),
                              m_editorCamera.getDistance(),
                              pos, sz, 0);

    if (std::memcmp(&view, &before, sizeof(glm::mat4)) != 0)
        m_editorCamera.setViewMatrix(view);
}

void ViewportPanel::drawCameraFrustum(const std::shared_ptr<Scene>& scene,
                                      const ImVec2& viewportPos,
                                      const ImVec2& size)
{
    if (!scene) return;

    const EntityID cameraId = scene->getPrimaryCameraID();
    if (cameraId == NULL_ENTITY)
        return;

    auto* targetCam = scene->getComponent<CameraComponent>(cameraId);
    if (!targetCam)
        return;

    targetCam->camera.setViewMatrix(glm::inverse(scene->getWorldTransform(cameraId)));

    glm::mat4 inv = glm::inverse(targetCam->camera.getProjectionMatrix() *
                                 targetCam->camera.getViewMatrix());

    std::array<glm::vec3, 8> cornersNDC = {{
        {-1, -1, 0}, { 1, -1, 0}, { 1,  1, 0}, {-1,  1, 0}, // near
        {-1, -1, 1}, { 1, -1, 1}, { 1,  1, 1}, {-1,  1, 1}  // far
    }};
    std::array<glm::vec3, 8> cornersWorld{};
    std::array<bool, 8> worldValid{};
    for (size_t i = 0; i < cornersNDC.size(); ++i) {
        glm::vec4 w = inv * glm::vec4(cornersNDC[i], 1.0f);
        if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z) ||
            !std::isfinite(w.w) || std::abs(w.w) < 1e-6f)
        {
            worldValid[i] = false;
            continue;
        }
        cornersWorld[i] = glm::vec3(w) / w.w;
        worldValid[i] = std::isfinite(cornersWorld[i].x) &&
                        std::isfinite(cornersWorld[i].y) &&
                        std::isfinite(cornersWorld[i].z);
    }

    glm::mat4 editorVP = m_editorCamera.getProjectionMatrix() * m_editorCamera.getViewMatrix();

    auto project = [&](const glm::vec3& world, ImVec2& out) -> bool {
        if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
            return false;
        glm::vec4 clip = editorVP * glm::vec4(world, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w))
            return false;
        if (clip.w <= 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z))
            return false;
        out.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * size.x;
        out.y = viewportPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y;
        if (!std::isfinite(out.x) || !std::isfinite(out.y))
            return false;
        return true;
    };

    std::array<ImVec2, 8> cornersScreen{};
    std::array<bool, 8> valid{};
    for (size_t i = 0; i < cornersWorld.size(); ++i)
        valid[i] = worldValid[i] && project(cornersWorld[i], cornersScreen[i]);

    auto* dl = ImGui::GetWindowDrawList();
    ImU32 color = IM_COL32(255, 210, 80, 200);
    auto line = [&](int a, int b) {
        if (valid[a] && valid[b])
            dl->AddLine(cornersScreen[a], cornersScreen[b], color, 1.0f);
    };

    line(0, 1); line(1, 2); line(2, 3); line(3, 0);
    line(4, 5); line(5, 6); line(6, 7); line(7, 4);
    line(0, 4); line(1, 5); line(2, 6); line(3, 7);
}

EntityID ViewportPanel::pickEntityAtCenter(const std::shared_ptr<Scene>& scene) const
{
    if (!scene) return NULL_ENTITY;
    const float cx = (m_viewportBoundsMin.x + m_viewportBoundsMax.x) * 0.5f;
    const float cy = (m_viewportBoundsMin.y + m_viewportBoundsMax.y) * 0.5f;
    const float w  = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float h  = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (w <= 1.0f || h <= 1.0f) return NULL_ENTITY;
    const float ndcX = ((cx - m_viewportBoundsMin.x) / w) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((cy - m_viewportBoundsMin.y) / h) * 2.0f;
    auto [origin, dir] = m_editorCamera.castRay(ndcX, ndcY);
    return const_cast<Scene&>(*scene).pickEntity(origin, dir);
}

void ViewportPanel::renderDebugBar(const std::shared_ptr<Scene>& scene)
{
    const float vpW = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float vpH = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (vpW < 200.0f || vpH < 60.0f) return;

    // ── Palette (matches the reference debug overlay) ─────────────────────────
    constexpr ImU32 kYellow    = IM_COL32(255, 214,  10, 255);
    constexpr ImU32 kYellowDim = IM_COL32(198, 166,  16, 255);
    constexpr ImU32 kGreen     = IM_COL32(140, 240,  90, 255);
    constexpr ImU32 kWhite     = IM_COL32(238, 238, 238, 255);
    constexpr ImU32 kCyan      = IM_COL32(120, 216, 255, 255);
    constexpr ImU32 kRed       = IM_COL32(226,  32,  28, 255);
    constexpr ImU32 kShadow    = IM_COL32(  0,   0,   0, 225);

    auto* dl = ImGui::GetWindowDrawList();
    ImFont* mono = DemonTheme::monoFont();
    // Overlay type sizes. ImGui 1.92 rasterises on demand, so one face is enough.
    const float kColumnPx = 13.0f;
    const float kBarPx    = 15.0f;
    const float kBannerPx = 34.0f;
    auto pushMono = [&](float px) { if (mono) ImGui::PushFont(mono, px); };
    auto popMono  = [&]()         { if (mono) ImGui::PopFont(); };

    // Hard shadow behind every glyph so the overlay stays readable on bright sky.
    auto shadowText = [&](ImVec2 pos, ImU32 color, const char* text) {
        dl->AddText({pos.x + 1.0f, pos.y + 1.0f}, kShadow, text);
        dl->AddText(pos, color, text);
    };

    const Renderer& renderer = Application::get().getRenderer();
    const auto& stats = renderer.getStats();
    const auto& shadowSettings = renderer.settings().shadows;

    const bool runtimePaused = m_playPaused || !m_playMode;

    // ══ Left column ═══════════════════════════════════════════════════════════
    pushMono(kColumnPx);
    {
        const float lineH  = ImGui::GetTextLineHeight() + 1.0f;
        const float startX = m_viewportBoundsMin.x + 10.0f;
        float y            = m_viewportBoundsMin.y + 8.0f;
        const float maxY   = m_viewportBoundsMax.y - kDebugBarHeight - lineH;

        // label/value pair rendered as a fixed-pitch aligned column
        auto stat = [&](const char* label, const std::string& value, ImU32 valueCol) {
            if (y > maxY) return;
            const std::string line = std::format("{:<11}{}", label, value);
            shadowText({startX, y}, valueCol, line.c_str());
            // Re-draw just the label in the dim tone so the value reads louder.
            const std::string labelOnly = std::format("{:<11}", label);
            dl->AddText({startX, y}, kYellowDim, labelOnly.c_str());
            y += lineH;
        };
        auto header = [&](const char* text) {
            if (y > maxY) return;
            shadowText({startX, y}, kGreen, text);
            y += lineH;
        };
        auto rule = [&]() {
            if (y > maxY) return;
            shadowText({startX, y}, IM_COL32(96, 84, 20, 220), "------------------------------");
            y += lineH;
        };

        header(std::format("DEMON ENGINE v1.2  DEBUG VIEW  [{}]",
                           m_activeTab == ViewportTab::Game ? "GAME" : "SCENE").c_str());
        stat("scene", scene ? scene->getName() : std::string("<none>"), kWhite);
        stat("entities", scene ? std::format("{}", scene->getEntities().size()) : std::string("0"), kWhite);
        stat("state", m_playMode ? (m_playPaused ? "PLAYING (PAUSED)" : "PLAYING") : "EDIT",
             m_playMode ? (m_playPaused ? kYellow : kGreen) : kCyan);
        rule();

        const ImU32 fpsCol = (m_currentFps >= 50.0f) ? kGreen
                           : (m_currentFps >= 30.0f) ? kYellow
                                                     : kRed;
        stat("fps", std::format("{:.0f}   avg {:.0f}", m_currentFps, m_avgFps), fpsCol);
        stat("frame", std::format("{:.2f} ms", m_dt * 1000.0f), kYellow);
        if (stats.gpuTimeMs > 0.0f)
            stat("gpu", std::format("{:.2f} ms", stats.gpuTimeMs), kYellow);
        rule();

        header("RENDER");
        stat("draws", std::format("{}", stats.drawCalls), kYellow);
        stat("gbuffer", std::format("{}", stats.gbufferDrawCalls), kYellow);
        stat("verts", std::format("{}", stats.vertexCount), kYellow);
        stat("tris", std::format("{}", stats.indexCount / 3u), kYellow);
        stat("culled", std::format("{}  occl {}", stats.culledCount, stats.occludedCount), kYellow);
        stat("compute", std::format("{}  tiles {}", stats.computeDispatches, stats.computeTiles), kYellow);
        rule();

        header("SHADOWS");
        stat("cascades", shadowSettings.enabled ? "4 (2048 atlas)" : "disabled",
             shadowSettings.enabled ? kYellow : kRed);
        stat("distance", std::format("{:.0f} m", shadowSettings.maxDistance), kYellow);
        stat("strength", std::format("{:.2f}  soft {:.2f}", shadowSettings.strength, shadowSettings.softness), kYellow);
        rule();

        header("CAMERA");
        const glm::vec3 camPos = m_editorCamera.getPosition();
        const glm::vec3 camFwd = m_editorCamera.getForward();
        stat("position", std::format("{:8.2f} {:8.2f} {:8.2f}", camPos.x, camPos.y, camPos.z), kYellow);
        stat("forward", std::format("{:8.2f} {:8.2f} {:8.2f}", camFwd.x, camFwd.y, camFwd.z), kYellow);
        stat("fov", std::format("{:.1f} deg", m_editorCamera.getFovY()), kYellow);
        stat("viewport", std::format("{} x {}", m_width, m_height), kYellow);
    }
    popMono();

    // ══ "Debug Paused" banner (top right) ═════════════════════════════════════
    if (runtimePaused) {
        pushMono(kBannerPx);
        const char* pausedLabel = "Debug Paused";
        const ImVec2 textSize = ImGui::CalcTextSize(pausedLabel);
        const ImVec2 pos{ m_viewportBoundsMax.x - textSize.x - 18.0f,
                          m_viewportBoundsMin.y + 10.0f };
        // Heavy black outline: the reference banner sits directly on the scene.
        for (int ox = -2; ox <= 2; ++ox)
            for (int oy = -2; oy <= 2; ++oy)
                if (ox || oy)
                    dl->AddText({pos.x + static_cast<float>(ox), pos.y + static_cast<float>(oy)},
                                IM_COL32(0, 0, 0, 235), pausedLabel);
        dl->AddText(pos, kRed, pausedLabel);
        popMono();
    }

    // ══ Bottom status bar — solid black, yellow monospace ═════════════════════
    const ImVec2 barMin{ m_viewportBoundsMin.x, m_viewportBoundsMax.y - kDebugBarHeight };
    const ImVec2 barMax{ m_viewportBoundsMax.x, m_viewportBoundsMax.y };

    dl->AddRectFilled(barMin, barMax, IM_COL32(0, 0, 0, 255));
    dl->AddRectFilled(barMin, {barMax.x, barMin.y + 1.0f}, IM_COL32(255, 214, 10, 190));

    pushMono(kBarPx);
    {
        const float lineH = ImGui::GetTextLineHeight();
        const float row0  = barMin.y + 5.0f;
        const float row1  = row0 + lineH + 3.0f;
        const float leftX = barMin.x + 12.0f;

        const glm::vec3 camPos = m_editorCamera.getPosition();
        const std::string posLine = std::format("POS {:.2f} {:.2f} {:.2f}", camPos.x, camPos.y, camPos.z);
        dl->AddText({leftX, row0}, kYellow, posLine.c_str());

        const std::string sceneLine = std::format("SCENE {}   ENT {}",
                                                 scene ? scene->getName() : std::string("<none>"),
                                                 scene ? scene->getEntities().size() : size_t{0});
        dl->AddText({leftX, row1}, kYellowDim, sceneLine.c_str());

        // Centre cluster: framerate + draw load.
        const std::string fpsLine   = std::format("{:.0f} FPS   {:.2f} ms", m_currentFps, m_dt * 1000.0f);
        const std::string drawLine  = std::format("DRAWS {}   TRIS {}", stats.drawCalls, stats.indexCount / 3u);
        const float fpsW  = ImGui::CalcTextSize(fpsLine.c_str()).x;
        const float drawW = ImGui::CalcTextSize(drawLine.c_str()).x;
        const float centreX = barMin.x + vpW * 0.5f;
        dl->AddText({centreX - fpsW  * 0.5f, row0}, kYellow, fpsLine.c_str());
        dl->AddText({centreX - drawW * 0.5f, row1}, kYellowDim, drawLine.c_str());

        // Right cluster: memory + adapter.
        const std::string memLine = std::format("VRAM {:.0f}/{:.0f} MB   RAM {:.0f}/{:.0f} MB",
                                                m_vramUsedMB, m_vramTotalMB, m_ramUsedMB, m_ramTotalMB);
        std::string gpuLine = m_gpuName.empty() ? std::string("GPU detecting...") : m_gpuName;
        if (gpuLine.size() > 52)
            gpuLine = gpuLine.substr(0, 50) + "..";
        const float memW = ImGui::CalcTextSize(memLine.c_str()).x;
        const float gpuW = ImGui::CalcTextSize(gpuLine.c_str()).x;
        const float rightEdge = barMax.x - 12.0f;
        dl->AddText({rightEdge - memW, row0}, m_vramWarningActive ? kRed : kYellow, memLine.c_str());
        dl->AddText({rightEdge - gpuW, row1}, kYellowDim, gpuLine.c_str());
    }
    popMono();
}

void ViewportPanel::renderHoverObjectInfo(const std::shared_ptr<Scene>& scene)
{
    if (!scene || m_hoveredEntity == NULL_ENTITY) return;

    // Build info lines
    std::vector<std::string> lines;

    if (const auto* tag = scene->getComponent<TagComponent>(m_hoveredEntity))
        lines.push_back(std::format("tag: \"{}\"", tag->tag));

    if (const auto* tc = scene->getComponent<TransformComponent>(m_hoveredEntity)) {
        lines.push_back(std::format("pos  ({:.2f}, {:.2f}, {:.2f})",
            tc->translation.x, tc->translation.y, tc->translation.z));
        lines.push_back(std::format("rot  ({:.1f}, {:.1f}, {:.1f})",
            tc->rotation.x, tc->rotation.y, tc->rotation.z));
        lines.push_back(std::format("scl  ({:.2f}, {:.2f}, {:.2f})",
            tc->scale.x, tc->scale.y, tc->scale.z));
    }

    if (const auto* rb = scene->getComponent<RigidBodyComponent>(m_hoveredEntity)) {
        lines.push_back(std::format("rigidbody: mass={:.1f}  kinematic={}",
            rb->mass, rb->isKinematic ? "true" : "false"));
    }

    if (const auto* bc = scene->getComponent<BoxColliderComponent>(m_hoveredEntity)) {
        lines.push_back(std::format("boxcollider: ({:.2f}, {:.2f}, {:.2f})",
            bc->halfExtents.x, bc->halfExtents.y, bc->halfExtents.z));
    }

    if (const auto* mr = scene->getComponent<MeshRendererComponent>(m_hoveredEntity)) {
        const std::string meshFile = std::filesystem::path(mr->meshPath).filename().string();
        lines.push_back(std::format("mesh: \"{}\"", meshFile.empty() ? "none" : meshFile));
    }

    if (const auto* anim = scene->getComponent<AnimatorComponent>(m_hoveredEntity)) {
        lines.push_back(std::format("animator: clip=\"{}\"  t={:.2f}",
            anim->currentClip.empty() ? "none" : anim->currentClip,
            anim->currentTime));
    }

    if (const auto* cam = scene->getComponent<CameraComponent>(m_hoveredEntity))
        lines.push_back(std::format("camera: fov={:.1f}",
            cam->camera.getFovY()));

    if (const auto* light = scene->getComponent<LightComponent>(m_hoveredEntity))
        lines.push_back(std::format("light: intensity={:.2f}", light->intensity));

    if (lines.empty()) return;

    // Position: right side of viewport, vertically centered
    const float vpW = m_viewportBoundsMax.x - m_viewportBoundsMin.x;
    const float vpH = m_viewportBoundsMax.y - m_viewportBoundsMin.y;
    if (vpW < 200.0f || vpH < 60.0f) return;

    // Same fixed-pitch face as the debug column so the two overlays read as one.
    ImFont* mono = DemonTheme::monoFont();
    if (mono) ImGui::PushFont(mono, 13.0f);

    const float lineH    = ImGui::GetTextLineHeight() + 4.0f;
    const float padX     = 10.0f;
    const float padY     = 6.0f;

    // Measure max line width
    float maxW = 0.0f;
    for (const auto& l : lines)
        maxW = std::max(maxW, ImGui::CalcTextSize(l.c_str()).x);

    const float boxW = maxW + padX * 2.0f;
    const float boxH = static_cast<float>(lines.size()) * lineH + padY * 2.0f;
    const float boxX = m_viewportBoundsMax.x - boxW - 14.0f;
    // Keep the block clear of the solid debug status bar and the paused banner.
    const float topLimit    = m_viewportBoundsMin.y + 64.0f;
    const float bottomLimit = m_viewportBoundsMax.y - kDebugBarHeight - 8.0f;
    float boxY = m_viewportBoundsMin.y + (vpH - boxH) * 0.5f;
    boxY = std::clamp(boxY, topLimit, std::max(topLimit, bottomLimit - boxH));

    auto* dl = ImGui::GetWindowDrawList();

    // Per-line highlight boxes (no solid sidebar, just per-line pill)
    for (size_t i = 0; i < lines.size(); ++i) {
        const float lx  = boxX;
        const float ly  = boxY + padY + static_cast<float>(i) * lineH;
        const float lw  = ImGui::CalcTextSize(lines[i].c_str()).x + padX * 2.0f;
        const float lh  = ImGui::GetTextLineHeight() + 4.0f;

        // Solid black pill background to match the debug bar treatment.
        dl->AddRectFilled({ lx, ly - 2.0f }, { lx + lw, ly + lh - 2.0f },
                          IM_COL32(0, 0, 0, 235));

        // Left accent bar
        dl->AddRectFilled({ lx, ly - 2.0f }, { lx + 2.5f, ly + lh - 2.0f },
                          IM_COL32(255, 214, 10, 220));

        // Text
        ImU32 textCol = IM_COL32(198, 166, 16, 255);
        if (i == 0) textCol = IM_COL32(255, 214, 10, 255); // tag highlighted gold
        dl->AddText({ lx + padX, ly }, textCol, lines[i].c_str());
    }

    if (mono) ImGui::PopFont();
}

void ViewportPanel::onEvent(Event& /*e*/) {}

} // namespace Demon
