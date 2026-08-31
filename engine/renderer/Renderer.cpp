// ==============================================================================
//  DemonEngine::Renderer  –  DirectX 12 Implementation
// ==============================================================================
#include "Renderer.h"
#include "Material.h"
#include "Texture.h"
#include "Mesh.h"
#include "Camera.h"
#include "scene/Components.h"
#include "renderer/stb_image.h"
#include "serialization/Serialization.h"
#include "core/Logger.h"
#include "core/PackageIO.h"
#include <directx/d3dx12.h>
#include <cmath>
#include <fstream>
#include <limits>

namespace Demon {

static std::vector<uint8_t> readFileBytes(const std::string& path)
{
    return PackageIO::readRuntimeBinary(path);
}

Renderer::Renderer(Window& window) : m_window(window) {}
Renderer::~Renderer() { shutdown(); }

void Renderer::init()
{
    const bool enableDebug =
#ifdef DEMON_DX12_DEBUG
        true;
#else
        false;
#endif
    m_context.init(enableDebug);

    auto device = m_context.getDevice();
    m_rtvHeap.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 32, false);
    m_dsvHeap.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16, false);
    m_srvHeap.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, true);
    m_samplerHeap.init(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true);

    m_swapchain.init(m_context, m_window.getWin32Handle(), m_window.getWidth(), m_window.getHeight(),
                     m_rtvHeap, m_dsvHeap);

    m_postProcessing.init(m_context, m_srvHeap, m_rtvHeap, m_samplerHeap);

    // Create default samplers
    {
        D3D12_SAMPLER_DESC s{};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        m_samplerWrapIndex = m_samplerHeap.allocate(1);
        device->CreateSampler(&s, m_samplerHeap.cpuHandle(m_samplerWrapIndex));

        s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        m_samplerClampIndex = m_samplerHeap.allocate(1);
        device->CreateSampler(&s, m_samplerHeap.cpuHandle(m_samplerClampIndex));
    }

    initSceneConstantBuffer();
    initSkinningConstantBuffer();
    initShadowConstantBuffer();
    createScenePipeline();
    createGBufferPipeline();
    createComputeStack();
    createSkyPipeline();
    createShadowResources();

    DEMON_LOG_INFO("Renderer: DirectX 12 initialised.");
}

void Renderer::shutdown()
{
    if (!m_context.getDevice())
        return;

    m_context.waitForGpu();
    destroyUploadedMeshes();
    destroyUploadedMaterials();
    destroyScenePipeline();
    destroyGBufferPipeline();
    destroyComputeStack();
    destroySkyPipeline();
    destroyShadowResources();
    destroyViewportResources();
    m_postProcessing.shutdown();
    if (m_sceneCb) {
        m_sceneCb->Unmap(0, nullptr);
        m_sceneCb.Reset();
        m_sceneCbMapped = nullptr;
    }
    if (m_skinningCb) {
        m_skinningCb->Unmap(0, nullptr);
        m_skinningCb.Reset();
        m_skinningCbMapped = nullptr;
    }
    if (m_shadowCb) {
        m_shadowCb->Unmap(0, nullptr);
        m_shadowCb.Reset();
        m_shadowCbMapped = nullptr;
    }
    m_swapchain.cleanup();
    m_context.shutdown();
}

bool Renderer::beginFrame()
{
    m_stats = {};
    m_frameIndex = m_swapchain.getCurrentFrameIndex();
    m_context.waitForFrame(m_frameIndex);
    m_skinningSlotCursor = 1;
    if (m_skinningCbMapped) {
        const size_t frameBase = static_cast<size_t>(m_frameIndex) * static_cast<size_t>(k_maxAnimatedDraws + 1) * m_skinningCbStride;
        auto* identityMatrices = reinterpret_cast<glm::mat4*>(m_skinningCbMapped + frameBase);
        for (uint32_t matrixIndex = 0; matrixIndex < kMaxSkinningMatrices; ++matrixIndex)
            identityMatrices[matrixIndex] = glm::mat4(1.0f);
    }

    if (m_swapchainResizePending || m_viewportResizePending) {
        DEMON_LOG_INFO("Renderer: discarding stale command list state before pending resize work.");
        m_context.discardFrame(m_frameIndex);
    }

    if (m_swapchainResizePending) {
        DEMON_LOG_INFO("Renderer: beginFrame applying pending swapchain resize {}x{}",
                       m_pendingSwapchainWidth, m_pendingSwapchainHeight);
        if (m_swapchain.resize(m_pendingSwapchainWidth, m_pendingSwapchainHeight)) {
            m_swapchainResizePending = false;
            m_frameIndex = m_swapchain.getCurrentFrameIndex();
            m_context.waitForFrame(m_frameIndex);
            DEMON_LOG_INFO("Renderer: swapchain resize applied successfully.");
        } else {
            DEMON_LOG_WARN("Renderer: swapchain resize was not applied this frame.");
            return false;
        }
    }

    // Viewport resize must happen BEFORE beginFrame opens the command list,
    // and after frame synchronization so resources can be recreated safely.
    if (m_viewportResizePending) {
        DEMON_LOG_INFO("Renderer: beginFrame applying pending viewport resize {}x{}",
                       m_pendingViewportWidth, m_pendingViewportHeight);
        createViewportResources(m_pendingViewportWidth, m_pendingViewportHeight);
        m_viewportResizePending = false;
    }

    m_context.beginFrame(m_frameIndex);

    auto cmd = m_context.getCommandList();
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.get(), m_samplerHeap.get() };
    cmd->SetDescriptorHeaps(2, heaps);

    if (m_sceneCbMapped) {
        std::memcpy(m_sceneCbMapped + (static_cast<size_t>(m_sceneCbStride) * m_frameIndex),
                    &m_sceneUbo, sizeof(SceneUBO));
    }

    flushPendingUploads();
    renderViewport(cmd);

    auto backBuffer = m_swapchain.getBackBuffer(m_frameIndex);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_swapchain.getRTV(m_frameIndex);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_swapchain.getDSV();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const float clear[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
    cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    if (m_viewportOutput && m_viewportOutput->isValid())
        m_postProcessing.present(cmd, *m_viewportOutput, rtv, m_window.getWidth(), m_window.getHeight());

    m_frameStarted = true;
    return true;
}

void Renderer::endFrame()
{
    if (!m_frameStarted) return;
    m_frameStarted = false;
    const uint32_t submittedFrameIndex = m_frameIndex;

    auto cmd = m_context.getCommandList();
    auto backBuffer = m_swapchain.getBackBuffer(m_frameIndex);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &barrier);

    m_context.endFrame();
    ID3D12CommandList* lists[] = { cmd };
    m_context.getCommandQueue()->ExecuteCommandLists(1, lists);
    m_context.signalFence(submittedFrameIndex);
    if (!m_skipPresentOnResize) {
        m_swapchain.present(m_window.isVSync());
        m_frameIndex = m_swapchain.getCurrentFrameIndex();
    }
    m_skipPresentOnResize = false;
}

void Renderer::beginScene(const Camera& camera)
{
    const auto& postProcessing = m_postProcessing;
    const auto& taa = postProcessing.settings().taa;
    const auto& motionBlur = postProcessing.settings().motionBlur;
    const bool taaActive = postProcessing.isEnabled() && taa.enabled;
    const bool temporalStateChanged =
        m_previousPostProcessingEnabled != postProcessing.isEnabled() ||
        m_previousTaaEnabled != taa.enabled ||
        m_previousMotionBlurEnabled != motionBlur.enabled ||
        std::fabs(m_previousTaaJitterScale - taa.jitterScale) > 1e-4f;
    if (temporalStateChanged)
        m_temporalResetPending = true;

    m_viewMatrix = camera.getViewMatrix();
    m_projectionMatrix = camera.getProjectionMatrix();
    m_unjitteredViewProjection = m_projectionMatrix * m_viewMatrix;
    m_viewProjection = m_unjitteredViewProjection;
    m_currentJitter = {0.0f, 0.0f};

    if (taaActive && m_viewportWidth > 0 && m_viewportHeight > 0) {
        const glm::vec2 halton = halton23(m_frameIndex + 1) - glm::vec2(0.5f);
        m_currentJitter = halton * taa.jitterScale;
        glm::mat4 jitteredProjection = m_projectionMatrix;
        const glm::vec2 jitterNdc = glm::vec2(
            (m_currentJitter.x * 2.0f) / static_cast<float>(m_viewportWidth),
            (m_currentJitter.y * 2.0f) / static_cast<float>(m_viewportHeight));
        jitteredProjection[2][0] += jitterNdc.x;
        jitteredProjection[2][1] += jitterNdc.y;
        m_viewProjection = jitteredProjection * m_viewMatrix;
    }

    m_sceneUbo.viewProj = m_viewProjection;
    m_sceneUbo.previousViewProj = (m_hasPreviousFrame && !m_temporalResetPending)
        ? m_previousViewProjection
        : m_viewProjection;
    m_sceneUbo.cameraPos = glm::vec4(camera.getPosition(), 1.0f);
    m_sceneUbo.localLightCount = glm::vec4(0.0f);
    m_cameraNearClip = camera.getNearClip();
    m_cameraFarClip = camera.getFarClip();
    extractFrustumPlanes(m_unjitteredViewProjection);
    m_drawList.clear();
}

void Renderer::setSceneTime(float timeSeconds)
{
    m_sceneUbo.cameraPos.w = timeSeconds;
}

void Renderer::setSceneLighting(const glm::vec3& direction,
                                const glm::vec3& color,
                                const glm::vec3& ambient,
                                bool directionalCastsShadows)
{
    if (glm::length(direction) > 0.0001f)
        m_sceneUbo.lightDir = glm::vec4(glm::normalize(direction), directionalCastsShadows ? 1.0f : 0.0f);
    m_sceneUbo.lightColor = glm::vec4(color, 1.0f);
    m_sceneUbo.ambient    = glm::vec4(ambient, 1.0f);
}

void Renderer::setLocalLights(const std::vector<LocalLight>& lights)
{
    const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(lights.size()), k_maxLocalLights);
    for (uint32_t i = 0; i < count; ++i)
        m_sceneUbo.localLights[i] = lights[i];
    for (uint32_t i = count; i < k_maxLocalLights; ++i)
        m_sceneUbo.localLights[i] = {};
    m_sceneUbo.localLightCount = glm::vec4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);
}

void Renderer::setDDGIProbes(const std::vector<DDGIProbe>& probes,
                             float intensity,
                             float normalBias,
                             float leakReduction,
                             bool enabled)
{
    const uint32_t count = enabled
        ? std::min<uint32_t>(static_cast<uint32_t>(probes.size()), k_maxDDGIProbes)
        : 0u;
    for (uint32_t i = 0; i < count; ++i)
        m_sceneUbo.ddgiProbes[i] = probes[i];
    for (uint32_t i = count; i < k_maxDDGIProbes; ++i)
        m_sceneUbo.ddgiProbes[i] = {};

    m_sceneUbo.ddgiParams = glm::vec4(enabled && count > 0 ? 1.0f : 0.0f,
                                      static_cast<float>(count),
                                      std::max(intensity, 0.0f),
                                      std::max(normalBias, 0.0f));
    m_sceneUbo.ddgiParams1 = glm::vec4(std::clamp(leakReduction, 0.0f, 1.0f),
                                       0.0f,
                                       0.0f,
                                       0.0f);
}

void Renderer::setSkybox(const std::string& texturePath, float intensity, bool enabled)
{
    const bool analyticSkyEnabled = m_settings.atmosphere.enabled;
    m_skyEnabled   = enabled || analyticSkyEnabled;
    m_skyIntensity = intensity;

    if (!m_skyEnabled) {
        // Don't clear m_skyPath so re-enabling doesn't force a reload.
        // Do clear skyAmbient so skylight turns off.
        m_sceneUbo.skyAmbient = {0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }

    // Ensure default textures exist — they may not yet if no material has been uploaded.
    // We need them to fill SRV table slots 1-3 for the sky root signature.
    if (!m_whiteTexture)  m_whiteTexture  = Texture::createWhite1x1(m_context, m_srvHeap);
    if (!m_blackTexture)  m_blackTexture  = Texture::createBlack1x1(m_context, m_srvHeap);
    if (!m_normalTexture) m_normalTexture = Texture::createNormal1x1(m_context, m_srvHeap);

    // Allocate SRV table before loading, so the table always exists even on failure.
    if (m_skySrvTableIndex == UINT32_MAX)
        m_skySrvTableIndex = m_srvHeap.allocate(4);

    auto writeSkyTable = [&](const std::shared_ptr<Texture>& texture) {
        auto device = m_context.getDevice();
        const auto srcHandle = texture ? texture->getSrvCpuHandle() : m_blackTexture->getSrvCpuHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap.cpuHandle(m_skySrvTableIndex);
        device->CopyDescriptorsSimple(1, dst, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (uint32_t i = 1; i < 4; ++i) {
            dst = m_srvHeap.cpuHandle(m_skySrvTableIndex + i);
            device->CopyDescriptorsSimple(1, dst, m_blackTexture->getSrvCpuHandle(),
                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    };

    const glm::vec3 analyticAverage = glm::mix(glm::vec3(0.16f, 0.22f, 0.34f),
                                               glm::vec3(0.55f, 0.62f, 0.78f),
                                               0.58f) * (0.75f + m_settings.atmosphere.sunDiskIntensity * 0.015f);

    if (texturePath.empty()) {
        m_skyPath.clear();
        m_skyTexture.reset();
        m_skyAverage = analyticAverage;
        writeSkyTable(nullptr);
    }

    else if (m_skyPath != texturePath || !m_skyTexture) {
        m_skyPath    = texturePath;
        m_skyTexture = Texture::loadFromFile(m_skyPath, m_context, m_srvHeap, false); // sRGB=false: HDR/linear sky

        // Compute average sky color for image-based skylight contribution
        m_skyAverage = {0.0f, 0.0f, 0.0f};
        auto img = stb_load_rgba(m_skyPath.c_str());
        if (img.pixels && img.w > 0 && img.h > 0) {
            const uint64_t count = static_cast<uint64_t>(img.w) * static_cast<uint64_t>(img.h);
            uint64_t r = 0, g = 0, b = 0;
            for (uint64_t i = 0; i < count; ++i) {
                r += img.pixels[i * 4 + 0];
                g += img.pixels[i * 4 + 1];
                b += img.pixels[i * 4 + 2];
            }
            const float inv = 1.0f / (255.0f * static_cast<float>(count));
            m_skyAverage = { static_cast<float>(r) * inv,
                             static_cast<float>(g) * inv,
                             static_cast<float>(b) * inv };
            stb_free(img.pixels);
        }

        // Write sky texture into slot 0 of the SRV table.
        // Slots 1-3 are padding (sky shader only reads t0) — fill with black.
        if (m_skyTexture) {
            writeSkyTable(m_skyTexture);
        } else {
            DEMON_LOG_WARN("Renderer: sky texture failed to load: '{}'", m_skyPath);
            writeSkyTable(nullptr);
        }
    }

    const float blend = analyticSkyEnabled ? glm::clamp(m_settings.atmosphere.skyBlend, 0.0f, 1.0f) : 0.0f;
    const glm::vec3 blendedAverage = glm::mix(m_skyAverage, analyticAverage, blend * 0.6f);
    m_sceneUbo.skyAmbient = glm::vec4(blendedAverage * std::max(m_skyIntensity, 0.001f) * m_settings.ibl.diffuseIntensity,
                                      std::max(m_skyIntensity, 0.001f));
}

void Renderer::setFog(const FogComponent& fog)
{
    m_fog = fog;
    if (m_fog.enabled) {
        m_sceneUbo.fogColorDensity = glm::vec4(m_fog.color, m_fog.density);
        m_sceneUbo.fogParams = glm::vec4(m_fog.height, m_fog.heightFalloff,
                                         m_fog.start, 1.0f);
    } else {
        m_sceneUbo.fogColorDensity = {0.0f, 0.0f, 0.0f, 0.0f};
        m_sceneUbo.fogParams = {0.0f, 0.0f, 0.0f, 0.0f};
    }
}

void Renderer::setVolumetricFog(const VolumetricFogComponent* fog)
{
    auto& settings = m_postProcessing.settings().volumetric;
    if (!fog || !fog->enabled) {
        settings.enabled = false;
        return;
    }

    settings.enabled = true;
    settings.color = fog->color;
    settings.density = std::clamp(fog->density, 0.0f, 0.25f);
    settings.intensity = std::clamp(fog->intensity, 0.0f, 2.0f);
    settings.anisotropy = std::clamp(fog->anisotropy, -0.85f, 0.85f);
    settings.startDistance = std::max(0.0f, fog->startDistance);
    settings.height = fog->height;
    settings.heightFalloff = std::clamp(fog->heightFalloff, 0.01f, 2.0f);
    settings.maxOpacity = std::clamp(fog->maxOpacity, 0.0f, 0.95f);
}

void Renderer::setLocalVolumetricFog(const LocalVolumetricFogComponent* fog, const glm::vec3& worldCenter)
{
    auto& settings = m_postProcessing.settings().volumetric;
    if (!fog || !fog->enabled) {
        settings.localEnabled = false;
        return;
    }

    settings.localEnabled = true;
    settings.localCenter = worldCenter;
    settings.localExtents = glm::max(fog->extents, glm::vec3(0.05f));
    settings.localColor = glm::clamp(fog->color, glm::vec3(0.0f), glm::vec3(1.0f));
    settings.localDensity = std::clamp(fog->density, 0.0f, 0.3f);
    settings.localIntensity = std::clamp(fog->intensity, 0.0f, 3.0f);
    settings.localEdgeSoftness = std::clamp(fog->edgeSoftness, 0.05f, 2.0f);
}

void Renderer::setVolumetricClouds(const VolumetricCloudComponent* clouds)
{
    auto& settings = m_postProcessing.settings().clouds;
    if (!clouds || !clouds->enabled) {
        settings.enabled = false;
        return;
    }

    settings.enabled = true;
    settings.coverage = std::clamp(clouds->coverage, 0.0f, 1.0f);
    settings.density = std::clamp(clouds->density, 0.0f, 1.5f);
    settings.altitude = std::clamp(clouds->altitude, 1.0f, 2000.0f);
    settings.thickness = std::clamp(clouds->thickness, 1.0f, 1000.0f);
    settings.scale = std::clamp(clouds->scale, 0.05f, 4.0f);
    settings.speed = std::clamp(clouds->speed, -0.5f, 0.5f);
    settings.darkness = std::clamp(clouds->darkness, 0.0f, 1.0f);
    settings.tint = clouds->tint;
}

void Renderer::setLensFlare(const LensFlareComponent* lensFlare)
{
    auto& settings = m_postProcessing.settings().lensFlare;
    if (!lensFlare || !lensFlare->enabled) {
        settings.enabled = false;
        return;
    }

    settings.enabled = true;
    settings.intensity = std::clamp(lensFlare->intensity, 0.0f, 4.0f);
    settings.threshold = std::clamp(lensFlare->threshold, 0.0f, 2.0f);
    settings.haloWidth = std::clamp(lensFlare->haloWidth, 0.01f, 2.0f);
    settings.ghostSpacing = std::clamp(lensFlare->ghostSpacing, 0.05f, 2.0f);
    settings.dirtIntensity = std::clamp(lensFlare->dirtIntensity, 0.0f, 1.0f);
    settings.tint = lensFlare->tint;
}

void Renderer::setReflectionProbe(const std::string& assetPath, const glm::vec3& center, bool enabled, int priority)
{
    const std::string previousAssetPath = m_settings.reflectionProbe.assetPath;
    m_settings.reflectionProbe.center = center;
    m_settings.reflectionProbe.priority = priority;
    m_settings.reflectionProbe.assetPath = assetPath;

    if (!enabled || assetPath.empty()) {
        m_settings.reflectionProbe.enabled = false;
        m_settings.reflectionProbe.texturePath.clear();
        m_reflectionProbeTexture.reset();
        return;
    }

    if (assetPath != previousAssetPath || !m_reflectionProbeTexture) {
        m_settings.reflectionProbe.texturePath.clear();
        m_reflectionProbeTexture.reset();

        std::ifstream file(assetPath, std::ios::binary);
        if (!file.is_open()) {
            DEMON_LOG_WARN("Renderer: reflection probe asset missing '{}'.", assetPath);
            m_settings.reflectionProbe.enabled = false;
            return;
        }

        std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        JsonDocument doc;
        if (!doc.parse(json) || !doc.root().isObject()) {
            DEMON_LOG_WARN("Renderer: reflection probe asset parse failed '{}'.", assetPath);
            m_settings.reflectionProbe.enabled = false;
            return;
        }

        const JsonValue& root = doc.root();
        if (const JsonValue* intensity = root.find("intensity"); intensity && intensity->isNumber())
            m_settings.reflectionProbe.intensity = static_cast<float>(intensity->asNumber(1.0));
        if (const JsonValue* extents = root.find("boxExtents"); extents && extents->isArray() && extents->asArray().size() >= 3) {
            m_settings.reflectionProbe.extents = glm::vec3(
                static_cast<float>(extents->asArray()[0].asNumber(5.0)),
                static_cast<float>(extents->asArray()[1].asNumber(5.0)),
                static_cast<float>(extents->asArray()[2].asNumber(5.0)));
        }
        if (const JsonValue* blend = root.find("blend"); blend && blend->isNumber())
            m_settings.reflectionProbe.blend = static_cast<float>(blend->asNumber(0.65));
        if (const JsonValue* assetPriority = root.find("priority"); assetPriority && assetPriority->isNumber())
            m_settings.reflectionProbe.priority = static_cast<int>(assetPriority->asNumber(static_cast<double>(priority)));

        std::filesystem::path texturePath;
        if (const JsonValue* tex = root.find("texturePath"); tex && tex->isString()) {
            texturePath = tex->asString();
            if (texturePath.is_relative())
                texturePath = std::filesystem::path(assetPath).parent_path() / texturePath;
        }

        if (!texturePath.empty()) {
            m_settings.reflectionProbe.texturePath = texturePath.string();
            m_reflectionProbeTexture = Texture::loadFromFile(m_settings.reflectionProbe.texturePath, m_context, m_srvHeap, false);
        }
    }

    m_settings.reflectionProbe.enabled = (m_reflectionProbeTexture != nullptr);
}

void Renderer::submit(const Mesh& mesh,
                      const Material& mat,
                      const glm::mat4& transform,
                      const std::vector<glm::mat4>* skinMatrices,
                      bool highlight,
                      uint64_t entityId,
                      int32_t subMeshIndex,
                      bool castShadows,
                      bool editorOverlay)
{
    // Frustum culling (sphere)
    {
        glm::vec3 center = glm::vec3(transform * glm::vec4(mesh.getBoundsCenter(), 1.0f));
        float scaleX = glm::length(glm::vec3(transform[0]));
        float scaleY = glm::length(glm::vec3(transform[1]));
        float scaleZ = glm::length(glm::vec3(transform[2]));
        float radius = mesh.getBoundsRadius() * std::max(std::max(scaleX, scaleY), scaleZ);
        if (!isSphereInFrustum(center, radius)) {
            m_stats.culledCount++;
            return;
        }
    }

    if (!mesh.uploaded)
        m_pendingMeshUploads.push_back(&mesh);
    if (!mat.uploaded || mat.isDirty())
        m_pendingMaterialUploads.push_back(&mat);

    const auto& md = mat.getData();
    DrawCommand dc;
    dc.mesh      = &mesh;
    dc.material  = &mat;
    dc.transform = transform;
    dc.albedo    = md.albedoColor;
    dc.roughness = md.roughness;
    dc.metallic  = md.metallic;
    dc.highlight = highlight ? 1.0f : 0.0f;
    dc.ao        = md.ao;
    dc.texFlags.x = (!mat.getAlbedoPath().empty()   && mat.albedoTexture)   ? 1.0f : 0.0f;
    dc.texFlags.y = (!mat.getNormalPath().empty()    && mat.normalTexture)   ? 1.0f : 0.0f;
    dc.texFlags.z = (!mat.getMetallicPath().empty()  && mat.metallicTexture) ? 1.0f : 0.0f;
    dc.texFlags.w = (!mat.getEmissivePath().empty()  && mat.emissiveTexture) ? 1.0f : 0.0f;
    dc.alphaBlend = md.alphaBlend || md.albedoColor.a < 0.999f;
    dc.castShadows = castShadows;
    dc.editorOverlay = editorOverlay;
    dc.previousTransform = transform;
    dc.skinMatrices = skinMatrices;
    dc.skinned = skinMatrices && !skinMatrices->empty();
    if (!m_temporalResetPending && entityId != 0) {
        auto it = m_previousTransforms.find(entityId);
        if (it != m_previousTransforms.end())
            dc.previousTransform = it->second;
    }
    dc.entityId   = entityId;
    dc.subMeshIndex = subMeshIndex;
    m_drawList.push_back(dc);
}

void Renderer::submitWater(const Mesh& mesh,
                           const Material& mat,
                           const glm::mat4& transform,
                           const WaterRenderParams& params,
                           uint64_t entityId)
{
    {
        glm::vec3 center = glm::vec3(transform * glm::vec4(mesh.getBoundsCenter(), 1.0f));
        float scaleX = glm::length(glm::vec3(transform[0]));
        float scaleY = glm::length(glm::vec3(transform[1]));
        float scaleZ = glm::length(glm::vec3(transform[2]));
        float radius = mesh.getBoundsRadius() * std::max(std::max(scaleX, scaleY), scaleZ);
        if (!isSphereInFrustum(center, radius)) {
            m_stats.culledCount++;
            return;
        }
    }

    if (!mesh.uploaded)
        m_pendingMeshUploads.push_back(&mesh);
    if (!mat.uploaded || mat.isDirty())
        m_pendingMaterialUploads.push_back(&mat);

    DrawCommand dc;
    dc.mesh = &mesh;
    dc.material = &mat;
    dc.transform = transform;
    dc.albedo = params.deepColorAndTransparency;
    dc.roughness = params.waveParams.x;
    dc.metallic = params.waveParams.y;
    dc.highlight = params.waveParams.z;
    dc.ao = params.waveParams.w;
    dc.texFlags = params.surfaceParams;
    dc.customData = params.styleParams;
    dc.alphaBlend = true;
    dc.castShadows = false;
    dc.previousTransform = transform;
    if (!m_temporalResetPending && entityId != 0) {
        auto it = m_previousTransforms.find(entityId);
        if (it != m_previousTransforms.end())
            dc.previousTransform = it->second;
    }
    dc.entityId = entityId;
    dc.pipeline = DrawPipeline::Water;
    m_drawList.push_back(dc);
}

void Renderer::endScene() {}

void Renderer::onResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    if (width == m_swapchain.getWidth() && height == m_swapchain.getHeight() && !m_swapchainResizePending)
        return;
    if (m_swapchainResizePending && width == m_pendingSwapchainWidth && height == m_pendingSwapchainHeight)
        return;

    DEMON_LOG_INFO("Renderer: queued swapchain resize {}x{} (frameStarted={})",
                   width, height, m_frameStarted);
    m_pendingSwapchainWidth  = width;
    m_pendingSwapchainHeight = height;
    m_swapchainResizePending = true;
    m_temporalResetPending = true;
    if (m_frameStarted)
        m_skipPresentOnResize = true;
}

void Renderer::resizeViewport(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    if (width == m_viewportWidth && height == m_viewportHeight && !m_viewportResizePending)
        return;
    if (m_viewportResizePending && width == m_pendingViewportWidth && height == m_pendingViewportHeight)
        return;

    m_pendingViewportWidth  = width;
    m_pendingViewportHeight = height;
    m_viewportResizePending = true;
    m_temporalResetPending = true;
}

void Renderer::recreateSwapchain()
{
    RECT rect{};
    if (!GetClientRect(m_window.getWin32Handle(), &rect))
        return;
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w == 0 || h == 0) return;
    m_swapchain.resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

void Renderer::createScenePipeline()
{
    destroyScenePipeline();

    if (!m_rootSignature) {
        CD3DX12_DESCRIPTOR_RANGE cbvRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
        CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
        CD3DX12_DESCRIPTOR_RANGE shadowSrvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 4);
        CD3DX12_DESCRIPTOR_RANGE sampRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

        CD3DX12_ROOT_PARAMETER params[6];
        params[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
        params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        params[2].InitAsDescriptorTable(1, &sampRange, D3D12_SHADER_VISIBILITY_PIXEL);
        params[3].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_VERTEX); // b2
        params[4].InitAsConstants(48, 1, 0, D3D12_SHADER_VISIBILITY_ALL); // b1
        params[5].InitAsDescriptorTable(1, &shadowSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_ROOT_SIGNATURE_DESC desc{};
        desc.Init(_countof(params), params, 0, nullptr,
                  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        Microsoft::WRL::ComPtr<ID3DBlob> sig;
        Microsoft::WRL::ComPtr<ID3DBlob> err;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        DEMON_ASSERT(SUCCEEDED(hr), "Failed to serialize root signature");
        hr = m_context.getDevice()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                        IID_PPV_ARGS(&m_rootSignature));
        DEMON_ASSERT(SUCCEEDED(hr), "Failed to create root signature");
    }

    auto vs = readFileBytes("assets/shaders/dx12/simple_vs.cso");
    auto ps = readFileBytes("assets/shaders/dx12/simple_ps.cso");
    DEMON_ASSERT(!vs.empty() && !ps.empty(), "Missing DX12 shader bytecode (simple)");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(Vertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, static_cast<UINT>(offsetof(Vertex, boneIndices)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, boneWeights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = { vs.data(), vs.size() };
    pso.PS = { ps.data(), ps.size() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = TRUE; // CCW winding (GLM/OpenGL convention)
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = k_sceneColorFormat;
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    pso.DSVFormat = m_swapchain.getDepthFormat();
    pso.SampleDesc.Count = 1;

    HRESULT hr = m_context.getDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_scenePso));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create scene PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPso = pso;
    auto& renderTargetBlend = transparentPso.BlendState.RenderTarget[0];
    renderTargetBlend.BlendEnable = TRUE;
    renderTargetBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    renderTargetBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    renderTargetBlend.BlendOp = D3D12_BLEND_OP_ADD;
    renderTargetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    renderTargetBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    renderTargetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    renderTargetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    transparentPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    hr = m_context.getDevice()->CreateGraphicsPipelineState(&transparentPso, IID_PPV_ARGS(&m_sceneTransparentPso));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create transparent scene PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC overlayPso = transparentPso;
    overlayPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    overlayPso.DepthStencilState.DepthEnable = FALSE;
    overlayPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    overlayPso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = m_context.getDevice()->CreateGraphicsPipelineState(&overlayPso, IID_PPV_ARGS(&m_sceneOverlayPso));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create editor overlay PSO");

    auto waterVs = readFileBytes("assets/shaders/dx12/water_replacement_vs.cso");
    auto waterPs = readFileBytes("assets/shaders/dx12/water_replacement_ps.cso");
    if (!waterVs.empty() && !waterPs.empty()) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC waterPso = transparentPso;
        waterPso.VS = { waterVs.data(), waterVs.size() };
        waterPso.PS = { waterPs.data(), waterPs.size() };
        hr = m_context.getDevice()->CreateGraphicsPipelineState(&waterPso, IID_PPV_ARGS(&m_waterPso));
        DEMON_ASSERT(SUCCEEDED(hr), "Failed to create water scene PSO");
    } else {
        DEMON_LOG_WARN("Renderer: water shader bytecode not found, water will use the generic transparent pipeline.");
        m_waterPso.Reset();
    }
}

void Renderer::createGBufferPipeline()
{
    m_gbufferPso.Reset();
    if (!m_rootSignature)
        return;

    auto vs = readFileBytes("assets/shaders/dx12/simple_vs.cso");
    auto ps = readFileBytes("assets/shaders/dx12/gbuffer_ps.cso");
    if (vs.empty() || ps.empty()) {
        DEMON_LOG_WARN("Renderer: GBuffer shader bytecode not found; deferred GBuffer pass disabled until shaders are compiled.");
        return;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(Vertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, static_cast<UINT>(offsetof(Vertex, boneIndices)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, boneWeights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = { vs.data(), vs.size() };
    pso.PS = { ps.data(), ps.size() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = TRUE;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 5;
    pso.RTVFormats[0] = k_gbufferAlbedoFormat;
    pso.RTVFormats[1] = k_gbufferMaterialFormat;
    pso.RTVFormats[2] = k_gbufferNormalFormat;
    pso.RTVFormats[3] = k_gbufferEmissiveFormat;
    pso.RTVFormats[4] = k_gbufferVelocityFormat;
    pso.DSVFormat = m_swapchain.getDepthFormat();
    pso.SampleDesc.Count = 1;

    HRESULT hr = m_context.getDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_gbufferPso));
    if (FAILED(hr)) {
        DEMON_LOG_WARN("Renderer: failed to create GBuffer PSO (hr={:#010x}).", static_cast<uint32_t>(hr));
        m_gbufferPso.Reset();
    }
}

void Renderer::createComputeStack()
{
    destroyComputeStack();

    CD3DX12_DESCRIPTOR_RANGE depthSrvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE tileUavRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
    params[1].InitAsDescriptorTable(1, &depthSrvRange, D3D12_SHADER_VISIBILITY_ALL);
    params[2].InitAsDescriptorTable(1, &tileUavRange, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 0, nullptr);

    Microsoft::WRL::ComPtr<ID3DBlob> sig;
    Microsoft::WRL::ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (FAILED(hr)) {
        DEMON_LOG_WARN("Renderer: failed to serialize compute root signature.");
        return;
    }

    hr = m_context.getDevice()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                    IID_PPV_ARGS(&m_computeRootSignature));
    if (FAILED(hr)) {
        DEMON_LOG_WARN("Renderer: failed to create compute root signature.");
        m_computeRootSignature.Reset();
        return;
    }

    auto createComputePso = [&](const char* shaderPath,
                                const char* passName,
                                Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPso) {
        auto cs = readFileBytes(shaderPath);
        if (cs.empty()) {
            DEMON_LOG_WARN("Renderer: {} compute bytecode not found; pass disabled until shaders are compiled.", passName);
            return;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_computeRootSignature.Get();
        pso.CS = { cs.data(), cs.size() };

        const HRESULT psoHr = m_context.getDevice()->CreateComputePipelineState(&pso, IID_PPV_ARGS(&outPso));
        if (FAILED(psoHr)) {
            DEMON_LOG_WARN("Renderer: failed to create {} compute PSO (hr={:#010x}).", passName, static_cast<uint32_t>(psoHr));
            outPso.Reset();
        }
    };

    createComputePso("assets/shaders/dx12/gbuffer_tile_classify_cs.cso",
                     "GBuffer tile classify",
                     m_gbufferTileClassifyPso);
    createComputePso("assets/shaders/dx12/gbuffer_ssao_prepare_cs.cso",
                     "PHASE 3 SSAO tile prepare",
                     m_gbufferSsaoPreparePso);
    createComputePso("assets/shaders/dx12/hdr_luminance_reduce_cs.cso",
                     "PHASE 4 HDR luminance reduce",
                     m_hdrLuminanceReducePso);
}

void Renderer::createSkyPipeline()
{
    auto vs = readFileBytes("assets/shaders/dx12/sky_vs.cso");
    auto ps = readFileBytes("assets/shaders/dx12/sky_ps.cso");
    if (vs.empty() || ps.empty()) {
        DEMON_LOG_WARN("Renderer: sky shader(s) not found — skybox disabled.");
        m_skyEnabled = false;
        return;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(Vertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, static_cast<UINT>(offsetof(Vertex, boneIndices)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, boneWeights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = { vs.data(), vs.size() };
    pso.PS = { ps.data(), ps.size() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.FrontCounterClockwise = TRUE;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    // Sky renders into the offscreen HDR viewport texture, not the swapchain.
    pso.RTVFormats[0] = k_sceneColorFormat;
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    pso.DSVFormat     = m_swapchain.getDepthFormat(); // D32_FLOAT — matches viewport depth
    pso.SampleDesc.Count = 1;

    HRESULT hr = m_context.getDevice()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_skyPso));
    if (FAILED(hr)) {
        DEMON_LOG_WARN("Renderer: failed to create sky PSO — skybox disabled.");
        m_skyEnabled = false;
    }
}

void Renderer::destroyScenePipeline()
{
    m_scenePso.Reset();
    m_sceneTransparentPso.Reset();
    m_sceneOverlayPso.Reset();
    m_waterPso.Reset();
}

void Renderer::destroySkyPipeline()
{
    m_skyPso.Reset();
}

void Renderer::destroyGBufferPipeline()
{
    m_gbufferPso.Reset();
}

void Renderer::destroyComputeStack()
{
    m_gbufferTileClassifyPso.Reset();
    m_gbufferSsaoPreparePso.Reset();
    m_hdrLuminanceReducePso.Reset();
    m_computeRootSignature.Reset();
    destroyComputeResources();
}

void Renderer::createShadowResources()
{
    auto device = m_context.getDevice();

    RHITextureDesc shadowDesc{};
    shadowDesc.width = k_shadowAtlasSize;
    shadowDesc.height = k_shadowAtlasSize;
    shadowDesc.format = DXGI_FORMAT_R32_TYPELESS;
    shadowDesc.mipLevels = 1;
    shadowDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    shadowDesc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    shadowDesc.createDSV = true;
    shadowDesc.createSRV = true;
    shadowDesc.dsvFormatOverride = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.srvFormatOverride = DXGI_FORMAT_R32_FLOAT;
    shadowDesc.hasClearValue = true;
    shadowDesc.clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.clearValue.DepthStencil.Depth = 1.0f;
    shadowDesc.clearValue.DepthStencil.Stencil = 0;
    m_shadowAtlas.create(device, shadowDesc, nullptr, &m_dsvHeap, &m_srvHeap, "ShadowAtlas");

    createShadowPipeline();
}

void Renderer::createShadowPipeline()
{
    auto device = m_context.getDevice();

    auto vs = readFileBytes("assets/shaders/dx12/shadow_vs.cso");
    if (vs.empty()) {
        DEMON_LOG_WARN("Renderer: shadow_vs.cso not found - cascaded shadows disabled.");
        m_shadowPso.Reset();
        return;
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(Vertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, static_cast<UINT>(offsetof(Vertex, boneIndices)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, boneWeights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = m_rootSignature.Get();
    pso.VS = { vs.data(), vs.size() };
    pso.PS = { nullptr, 0 };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = TRUE;
    // Depth bias is applied in the pixel shader in world units (see SampleShadow),
    // so the rasterizer only needs slope-scaled bias to cover grazing surfaces.
    // A constant bias here as well used to double-bias and erase contact shadows.
    pso.RasterizerState.DepthBias = 0;
    pso.RasterizerState.SlopeScaledDepthBias = 2.0f;
    pso.RasterizerState.DepthBiasClamp = 0.0f;
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 0;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;

    HRESULT hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_shadowPso));
    if (FAILED(hr)) {
        DEMON_LOG_WARN("Renderer: failed to create shadow PSO - cascaded shadows disabled.");
        m_shadowPso.Reset();
    }
}

void Renderer::destroyShadowResources()
{
    m_shadowPso.Reset();
    m_shadowAtlas.destroy();
}

bool Renderer::reloadPipelines()
{
    if (!m_context.getDevice())
        return false;

    // Make sure no in-flight command list still references the old PSOs before
    // we release them.
    m_context.waitForGpu();

    // Each create*() below re-reads the freshly compiled .cso from disk. Root
    // signatures are preserved (createScenePipeline only builds one when it is
    // missing), so only shader-derived state is swapped out. Descriptor heaps
    // and render targets are untouched, which is why the shadow *atlas* texture
    // is kept and only its PSO (createShadowPipeline) is rebuilt.
    destroyScenePipeline();
    createScenePipeline();
    destroyGBufferPipeline();
    createGBufferPipeline();
    destroySkyPipeline();
    createSkyPipeline();
    createShadowPipeline();

    // Post-processing owns an independent PSO set (volumetric, SSGI, bloom,
    // TAA, tonemap, DOF...). Rebuild it from disk as well.
    m_postProcessing.reloadPipelines();

    const bool ok = m_scenePso && m_gbufferPso;
    if (ok)
        DEMON_LOG_INFO("Renderer: graphics + post-processing pipelines reloaded from disk.");
    else
        DEMON_LOG_ERROR("Renderer: pipeline reload failed - inspect DXC diagnostics.");
    return ok;
}

void Renderer::createGBufferResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    auto device = m_context.getDevice();

    auto createColorTarget = [&](RHITexture& target,
                                 DXGI_FORMAT format,
                                 const char* debugName,
                                 const std::array<float, 4>& clearColor) {
        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.mipLevels = 1;
        desc.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        desc.createRTV = true;
        desc.createSRV = true;
        desc.hasClearValue = true;
        desc.clearValue.Format = format;
        desc.clearValue.Color[0] = clearColor[0];
        desc.clearValue.Color[1] = clearColor[1];
        desc.clearValue.Color[2] = clearColor[2];
        desc.clearValue.Color[3] = clearColor[3];
        target.create(device, desc, &m_rtvHeap, nullptr, &m_srvHeap, debugName);
    };

    createColorTarget(m_gbufferAlbedo, k_gbufferAlbedoFormat, "GBufferAlbedoAO", { 0.0f, 0.0f, 0.0f, 1.0f });
    createColorTarget(m_gbufferMaterial, k_gbufferMaterialFormat, "GBufferMaterial", { 0.0f, 0.5f, 0.5f, 1.0f });
    createColorTarget(m_gbufferNormal, k_gbufferNormalFormat, "GBufferNormalOct", { 0.0f, 0.0f, 0.0f, 0.0f });
    createColorTarget(m_gbufferEmissive, k_gbufferEmissiveFormat, "GBufferEmissive", { 0.0f, 0.0f, 0.0f, 0.0f });
    createColorTarget(m_gbufferVelocity, k_gbufferVelocityFormat, "GBufferVelocity", { 0.0f, 0.0f, 0.0f, 0.0f });

    RHITextureDesc depthDesc{};
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.mipLevels = 1;
    depthDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    depthDesc.initialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    depthDesc.createDSV = true;
    depthDesc.createSRV = true;
    depthDesc.dsvFormatOverride = m_swapchain.getDepthFormat();
    depthDesc.srvFormatOverride = DXGI_FORMAT_R32_FLOAT;
    depthDesc.hasClearValue = true;
    depthDesc.clearValue.Format = m_swapchain.getDepthFormat();
    depthDesc.clearValue.DepthStencil.Depth = 1.0f;
    depthDesc.clearValue.DepthStencil.Stencil = 0;
    m_gbufferDepth.create(device, depthDesc, nullptr, &m_dsvHeap, &m_srvHeap, "GBufferDepth");
}

void Renderer::destroyGBufferResources()
{
    m_gbufferAlbedo.destroy();
    m_gbufferMaterial.destroy();
    m_gbufferNormal.destroy();
    m_gbufferEmissive.destroy();
    m_gbufferVelocity.destroy();
    m_gbufferDepth.destroy();
}

void Renderer::createComputeResources(uint32_t width, uint32_t height)
{
    destroyComputeResources();

    if (width == 0 || height == 0)
        return;

    m_gbufferTileCountX = (width + k_computeTileSize - 1) / k_computeTileSize;
    m_gbufferTileCountY = (height + k_computeTileSize - 1) / k_computeTileSize;
    const uint32_t tileCount = std::max(1u, m_gbufferTileCountX * m_gbufferTileCountY);
    const uint64_t bufferSize = static_cast<uint64_t>(tileCount) * sizeof(uint32_t);

    auto device = m_context.getDevice();
    auto createTileBuffer = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& buffer,
                                uint32_t& uavIndex,
                                const wchar_t* debugName) {
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&buffer));
        DEMON_ASSERT(SUCCEEDED(hr), "Failed to create compute tile buffer");
        if (buffer && debugName)
            buffer->SetName(debugName);

        if (uavIndex == UINT32_MAX)
            uavIndex = m_srvHeap.allocate(1);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = tileCount;
        uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(buffer.Get(), nullptr, &uavDesc, m_srvHeap.cpuHandle(uavIndex));
    };

    createTileBuffer(m_gbufferTileMask, m_gbufferTileMaskUavIndex, L"GBufferTileMask");
    createTileBuffer(m_ssaoTileMask, m_ssaoTileMaskUavIndex, L"SSAOTileMask");
    createTileBuffer(m_hdrLuminanceTiles, m_hdrLuminanceTilesUavIndex, L"HDRLuminanceTiles");
}

void Renderer::destroyComputeResources()
{
    m_gbufferTileMask.Reset();
    m_ssaoTileMask.Reset();
    m_hdrLuminanceTiles.Reset();
    m_gbufferTileCountX = 0;
    m_gbufferTileCountY = 0;
}

void Renderer::createViewportResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) return;
    DEMON_LOG_INFO("Renderer: createViewportResources {}x{}", width, height);

    if (m_viewportColor.isValid() || m_viewportDepth.isValid()) {
        DEMON_LOG_INFO("Renderer: preparing context before recreating viewport resources.");
        m_context.prepareForResize();
    }

    destroyViewportResources();

    m_viewportWidth = width;
    m_viewportHeight = height;

    auto device = m_context.getDevice();

    RHITextureDesc colorDesc{};
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = k_sceneColorFormat;
    colorDesc.mipLevels = 1;
    colorDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    colorDesc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    colorDesc.createRTV = true;
    colorDesc.createSRV = true;
    colorDesc.hasClearValue = true;
    colorDesc.clearValue.Format = colorDesc.format;
    colorDesc.clearValue.Color[0] = 0.08f;
    colorDesc.clearValue.Color[1] = 0.08f;
    colorDesc.clearValue.Color[2] = 0.10f;
    colorDesc.clearValue.Color[3] = 1.0f;

    m_viewportColor.create(device, colorDesc, &m_rtvHeap, nullptr, &m_srvHeap, "ViewportColor");

    RHITextureDesc velocityDesc = colorDesc;
    velocityDesc.format = DXGI_FORMAT_R16G16_FLOAT;
    velocityDesc.clearValue.Format = velocityDesc.format;
    velocityDesc.clearValue.Color[0] = 0.0f;
    velocityDesc.clearValue.Color[1] = 0.0f;
    velocityDesc.clearValue.Color[2] = 0.0f;
    velocityDesc.clearValue.Color[3] = 0.0f;
    m_viewportVelocity.create(device, velocityDesc, &m_rtvHeap, nullptr, &m_srvHeap, "ViewportVelocity");

    RHITextureDesc depthDesc{};
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.mipLevels = 1;
    depthDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    depthDesc.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    depthDesc.createDSV = true;
    depthDesc.createSRV = true;
    depthDesc.dsvFormatOverride = m_swapchain.getDepthFormat();
    depthDesc.srvFormatOverride = DXGI_FORMAT_R32_FLOAT;
    depthDesc.hasClearValue = true;
    depthDesc.clearValue.Format = m_swapchain.getDepthFormat();
    depthDesc.clearValue.DepthStencil.Depth = 1.0f;
    depthDesc.clearValue.DepthStencil.Stencil = 0;

    m_viewportDepth.create(device, depthDesc, nullptr, &m_dsvHeap, &m_srvHeap, "ViewportDepth");
    createGBufferResources(width, height);
    createComputeResources(width, height);

    m_viewportSrv = m_viewportColor.getSrvGpu();
    m_postProcessing.resize(width, height);
    m_postProcessing.invalidateHistory();
    m_hasPreviousFrame = false;
    m_temporalResetPending = true;
    m_previousJitter = glm::vec2(0.0f);
    DEMON_LOG_INFO("Renderer: viewport resources ready {}x{}", width, height);
}

void Renderer::destroyViewportResources()
{
    destroyComputeResources();
    destroyGBufferResources();
    m_viewportColor.destroy();
    m_viewportDepth.destroy();
    m_viewportVelocity.destroy();
    m_viewportSrv = {};
    m_viewportOutput = nullptr;
}

void Renderer::flushPendingUploads()
{
    for (const Mesh* mesh : m_pendingMeshUploads) {
        if (mesh && !mesh->uploaded)
            doUploadMesh(*mesh);
    }
    for (const Material* mat : m_pendingMaterialUploads) {
        if (mat && (!mat->uploaded || mat->isDirty()))
            doUploadMaterial(*mat);
    }
    m_pendingMeshUploads.clear();
    m_pendingMaterialUploads.clear();
}

void Renderer::doUploadMesh(const Mesh& mesh)
{
    auto& m = const_cast<Mesh&>(mesh);
    if (m.uploaded) return;

    const auto& verts = m.getVertices();
    const auto& idxs  = m.getIndices();
    if (verts.empty() || idxs.empty()) return;

    auto device = m_context.getDevice();
    const uint64_t vSize = sizeof(Vertex) * verts.size();
    const uint64_t iSize = sizeof(uint32_t) * idxs.size();

    auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto heapUpload  = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vSize);
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(iSize);

    Microsoft::WRL::ComPtr<ID3D12Resource> vbUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> ibUpload;

    HRESULT hr = device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m.vertexBuffer));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create vertex buffer");

    hr = device->CreateCommittedResource(
        &heapDefault, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m.indexBuffer));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create index buffer");

    hr = device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vbUpload));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create vertex upload buffer");

    hr = device->CreateCommittedResource(
        &heapUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ibUpload));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create index upload buffer");

    void* mapped = nullptr;
    CD3DX12_RANGE range(0, 0);
    vbUpload->Map(0, &range, &mapped);
    std::memcpy(mapped, verts.data(), vSize);
    vbUpload->Unmap(0, nullptr);

    ibUpload->Map(0, &range, &mapped);
    std::memcpy(mapped, idxs.data(), iSize);
    ibUpload->Unmap(0, nullptr);

    m_context.immediateSubmit([&](ID3D12GraphicsCommandList* cmd) {
        cmd->CopyBufferRegion(m.vertexBuffer.Get(), 0, vbUpload.Get(), 0, vSize);
        cmd->CopyBufferRegion(m.indexBuffer.Get(), 0, ibUpload.Get(), 0, iSize);
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m.vertexBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
            CD3DX12_RESOURCE_BARRIER::Transition(m.indexBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
        };
        cmd->ResourceBarrier(2, barriers);
    });

    m.vertexView.BufferLocation = m.vertexBuffer->GetGPUVirtualAddress();
    m.vertexView.StrideInBytes  = sizeof(Vertex);
    m.vertexView.SizeInBytes    = static_cast<UINT>(vSize);
    m.indexView.BufferLocation  = m.indexBuffer->GetGPUVirtualAddress();
    m.indexView.SizeInBytes     = static_cast<UINT>(iSize);
    m.indexView.Format          = DXGI_FORMAT_R32_UINT;

    m.uploaded = true;
    m_uploadedMeshes.insert(&m);
}

void Renderer::doUploadMaterial(const Material& mat)
{
    auto& m = const_cast<Material&>(mat);

    auto loadOrDefault = [&](const std::string& path, const std::shared_ptr<Texture>& fallback, bool srgb) {
        if (!path.empty()) {
            auto tex = Texture::loadFromFile(path, m_context, m_srvHeap, srgb);
            if (tex) return tex;
        }
        return fallback;
    };

    if (!m_whiteTexture)
        m_whiteTexture  = Texture::createWhite1x1(m_context, m_srvHeap);
    if (!m_blackTexture)
        m_blackTexture  = Texture::createBlack1x1(m_context, m_srvHeap);
    if (!m_normalTexture)
        m_normalTexture = Texture::createNormal1x1(m_context, m_srvHeap);

    if (m_defaultMaterialTableIndex == UINT32_MAX) {
        m_defaultMaterialTableIndex = m_srvHeap.allocate(4);
        auto device = m_context.getDevice();
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap.cpuHandle(m_defaultMaterialTableIndex);
        device->CopyDescriptorsSimple(1, dst, m_whiteTexture->getSrvCpuHandle(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dst = m_srvHeap.cpuHandle(m_defaultMaterialTableIndex + 1);
        device->CopyDescriptorsSimple(1, dst, m_normalTexture->getSrvCpuHandle(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dst = m_srvHeap.cpuHandle(m_defaultMaterialTableIndex + 2);
        device->CopyDescriptorsSimple(1, dst, m_whiteTexture->getSrvCpuHandle(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dst = m_srvHeap.cpuHandle(m_defaultMaterialTableIndex + 3);
        device->CopyDescriptorsSimple(1, dst, m_blackTexture->getSrvCpuHandle(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    m.albedoTexture   = loadOrDefault(m.getAlbedoPath(),  m_whiteTexture,  true);
    m.normalTexture   = loadOrDefault(m.getNormalPath(),  m_normalTexture, false);
    m.metallicTexture = loadOrDefault(m.getMetallicPath(), m_whiteTexture, false);
    m.emissiveTexture = loadOrDefault(m.getEmissivePath(), m_blackTexture, true);

    if (m.srvTableIndex == UINT32_MAX)
        m.srvTableIndex = m_srvHeap.allocate(4);

    auto device = m_context.getDevice();
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap.cpuHandle(m.srvTableIndex);
    device->CopyDescriptorsSimple(1, dst, m.albedoTexture->getSrvCpuHandle(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dst = m_srvHeap.cpuHandle(m.srvTableIndex + 1);
    device->CopyDescriptorsSimple(1, dst, m.normalTexture->getSrvCpuHandle(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dst = m_srvHeap.cpuHandle(m.srvTableIndex + 2);
    device->CopyDescriptorsSimple(1, dst, m.metallicTexture->getSrvCpuHandle(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dst = m_srvHeap.cpuHandle(m.srvTableIndex + 3);
    device->CopyDescriptorsSimple(1, dst, m.emissiveTexture->getSrvCpuHandle(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    m.uploaded = true;
    m.clearDirty();
    m_uploadedMaterials.insert(&m);
}

void Renderer::updateShadowCascades()
{
    m_sceneUbo.atmosphereParams0 = glm::vec4(m_settings.atmosphere.density,
                                             m_settings.atmosphere.heightFalloff,
                                             m_settings.atmosphere.anisotropy,
                                             m_settings.atmosphere.enabled ? 1.0f : 0.0f);
    m_sceneUbo.atmosphereParams1 = glm::vec4(m_settings.atmosphere.sunDiskIntensity,
                                             m_settings.atmosphere.skyBlend,
                                             m_settings.atmosphere.sunDiskSize,
                                             m_settings.atmosphere.aerialPerspective);
    m_sceneUbo.iblParams = glm::vec4(m_settings.ibl.diffuseIntensity,
                                     m_settings.ibl.specularIntensity,
                                     m_settings.ibl.reflectionBoost,
                                     m_settings.ibl.localProbeBlend);
    m_sceneUbo.reflectionProbeCenter = glm::vec4(m_settings.reflectionProbe.center,
                                                 m_settings.reflectionProbe.enabled ? m_settings.reflectionProbe.intensity : 0.0f);
    m_sceneUbo.reflectionProbeExtents = glm::vec4(m_settings.reflectionProbe.extents,
                                                  m_settings.reflectionProbe.enabled ? m_settings.reflectionProbe.blend : 0.0f);
    m_sceneUbo.giParams = glm::vec4(m_settings.gi.enabled ? 1.0f : 0.0f,
                                    m_settings.gi.diffuseBounce,
                                    m_settings.gi.specularBounce,
                                    m_settings.gi.colorBleed);
    m_sceneUbo.giColor = glm::vec4(m_settings.gi.tint,
                                   std::max(0.1f, m_settings.gi.radius));
    m_sceneUbo.ddgiParams1.w = static_cast<float>(m_settings.gi.mode);

    const bool shadowsEnabled = m_settings.shadows.enabled
                             && m_shadowPso
                             && m_shadowAtlas.isValid()
                             && m_sceneUbo.lightDir.w > 0.5f
                             && glm::length(glm::vec3(m_sceneUbo.lightDir)) > 0.0001f;

    m_sceneUbo.shadowParams0 = glm::vec4(shadowsEnabled ? 1.0f : 0.0f,
                                         m_settings.shadows.strength,
                                         1.0f / static_cast<float>(k_shadowAtlasSize),
                                         m_settings.shadows.softness);
    m_sceneUbo.shadowParams1 = glm::vec4(m_settings.shadows.bias,
                                         m_settings.shadows.normalBias,
                                         m_settings.shadows.maxDistance,
                                         static_cast<float>(k_shadowCascadeCount));

    if (!shadowsEnabled) {
        for (uint32_t i = 0; i < k_shadowCascadeCount; ++i) {
            m_shadowMatrices[i] = glm::mat4(1.0f);
            m_sceneUbo.lightViewProj[i] = m_shadowMatrices[i];
            m_shadowSplitDistances[i] = m_settings.shadows.maxDistance;
        }
        m_sceneUbo.shadowSplits = glm::vec4(m_settings.shadows.maxDistance);
        if (!m_shadowDisabledLogged) {
            m_shadowDisabledLogged = true;
            DEMON_LOG_WARN("Shadows disabled: settings={} pso={} atlas={} sunCasts={}.",
                           static_cast<int>(m_settings.shadows.enabled),
                           static_cast<int>(m_shadowPso != nullptr),
                           static_cast<int>(m_shadowAtlas.isValid()),
                           static_cast<int>(m_sceneUbo.lightDir.w > 0.5f));
        }
        return;
    }
    m_shadowDisabledLogged = false;

    const float nearClip = std::max(m_cameraNearClip, 0.05f);
    const float farClip = std::max(nearClip + 1.0f, std::min(m_cameraFarClip, m_settings.shadows.maxDistance));
    const float clipRange = farClip - nearClip;
    const float lambda = 0.65f;
    std::array<float, k_shadowCascadeCount + 1> cascadeEnds{};
    cascadeEnds[0] = nearClip;
    for (uint32_t i = 1; i <= k_shadowCascadeCount; ++i) {
        const float ratio = static_cast<float>(i) / static_cast<float>(k_shadowCascadeCount);
        const float logarithmic = nearClip * std::pow(farClip / nearClip, ratio);
        const float uniform = nearClip + clipRange * ratio;
        cascadeEnds[i] = glm::mix(uniform, logarithmic, lambda);
    }

    std::array<glm::vec3, 8> frustumCorners{};
    const glm::mat4 invViewProj = glm::inverse(m_unjitteredViewProjection);
    uint32_t cornerIndex = 0;
    for (uint32_t z = 0; z < 2; ++z) {
        for (uint32_t y = 0; y < 2; ++y) {
            for (uint32_t x = 0; x < 2; ++x) {
                const glm::vec4 clip = glm::vec4(x ? 1.0f : -1.0f,
                                                 y ? 1.0f : -1.0f,
                                                 z ? 1.0f : 0.0f,
                                                 1.0f);
                glm::vec4 world = invViewProj * clip;
                world /= world.w;
                frustumCorners[cornerIndex++] = glm::vec3(world);
            }
        }
    }

    const glm::vec3 lightDir = glm::normalize(glm::vec3(m_sceneUbo.lightDir));
    const glm::vec3 lightUp = std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);

    for (uint32_t cascade = 0; cascade < k_shadowCascadeCount; ++cascade) {
        const float sliceNear = cascadeEnds[cascade];
        const float sliceFar = cascadeEnds[cascade + 1];
        const float nearRatio = (sliceNear - nearClip) / clipRange;
        const float farRatio = (sliceFar - nearClip) / clipRange;

        std::array<glm::vec3, 8> cascadeCorners{};
        for (uint32_t i = 0; i < 4; ++i) {
            const glm::vec3 ray = frustumCorners[i + 4] - frustumCorners[i];
            cascadeCorners[i] = frustumCorners[i] + ray * nearRatio;
            cascadeCorners[i + 4] = frustumCorners[i] + ray * farRatio;
        }

        glm::vec3 centroid{0.0f};
        for (const auto& corner : cascadeCorners)
            centroid += corner;
        centroid /= static_cast<float>(cascadeCorners.size());

        float radius = 0.0f;
        for (const auto& corner : cascadeCorners)
            radius = std::max(radius, glm::length(corner - centroid));
        radius = std::max(radius, 4.0f);

        // Park the virtual light far enough behind the cascade that tall casters
        // standing *outside* the slice still fall inside the depth range, then clip
        // from just in front of it. The old code offset by radius*3 and then padded
        // the Z extents by another 40 units, which bloated the depth range and made
        // a fixed depth-space bias behave completely differently per cascade.
        const float casterHeadroom = std::max(radius * 3.0f, 60.0f);
        const glm::vec3 lightPos = centroid - lightDir * (radius + casterHeadroom);
        const glm::mat4 lightView = glm::lookAt(lightPos, centroid, lightUp);

        glm::vec3 minExtents(std::numeric_limits<float>::max());
        glm::vec3 maxExtents(std::numeric_limits<float>::lowest());
        for (const auto& corner : cascadeCorners) {
            const glm::vec3 cornerLS = glm::vec3(lightView * glm::vec4(corner, 1.0f));
            minExtents = glm::min(minExtents, cornerLS);
            maxExtents = glm::max(maxExtents, cornerLS);
        }

        // Square the XY bound and snap it to whole texels. Without snapping the
        // cascade slides sub-texel as the camera moves and the shadow edges crawl.
        const float tileSizeF = static_cast<float>(k_shadowAtlasSize / 2);
        const glm::vec2 centerLS = (glm::vec2(minExtents) + glm::vec2(maxExtents)) * 0.5f;
        const float halfSpan = std::max(std::max(maxExtents.x - minExtents.x,
                                                 maxExtents.y - minExtents.y) * 0.5f,
                                        radius);
        const float texelWorld = (halfSpan * 2.0f) / tileSizeF;
        const glm::vec2 snappedCenter = glm::floor(centerLS / texelWorld) * texelWorld;
        minExtents.x = snappedCenter.x - halfSpan;
        maxExtents.x = snappedCenter.x + halfSpan;
        minExtents.y = snappedCenter.y - halfSpan;
        maxExtents.y = snappedCenter.y + halfSpan;

        // glm::lookAt builds a right-handed light space that looks down -Z, so every
        // corner of the cascade has a NEGATIVE light-space Z. glm::ortho resolves to
        // orthoRH_ZO under GLM_FORCE_DEPTH_ZERO_TO_ONE and wants near/far as POSITIVE
        // distances along the view direction, so the Z extents must be negated and
        // swapped. Feeding it the raw negative extents mapped every caster to
        // z_clip > 1, i.e. behind the far plane, which left the atlas empty and made
        // the lighting pass treat every pixel as unshadowed.
        const float nearPlane = 1.0f;                             // casterHeadroom already sits in front of the slice
        const float farPlane  = -minExtents.z + radius;           // furthest corner plus a little slack

        const glm::mat4 lightProjection = glm::ortho(minExtents.x, maxExtents.x,
                                                     minExtents.y, maxExtents.y,
                                                     nearPlane, farPlane);

        m_sceneUbo.shadowDepthRange[cascade] = std::max(farPlane - nearPlane, 1e-4f);
        m_sceneUbo.shadowTexelWorld[cascade] = std::max(texelWorld, 1e-5f);

        m_shadowMatrices[cascade] = lightProjection * lightView;
        m_sceneUbo.lightViewProj[cascade] = m_shadowMatrices[cascade];
        m_shadowSplitDistances[cascade] = sliceFar;
        m_sceneUbo.shadowSplits[cascade] = sliceFar;

        if (m_shadowCbMapped) {
            ShadowSceneUBO shadowUbo{};
            shadowUbo.viewProj = m_shadowMatrices[cascade];
            const size_t offset = static_cast<size_t>(m_shadowCbStride)
                                * (m_frameIndex * k_shadowCascadeCount + cascade);
            std::memcpy(m_shadowCbMapped + offset, &shadowUbo, sizeof(ShadowSceneUBO));
        }
    }

    // Log the resolved cascade setup once, and again whenever the sun swings far
    // enough to change the layout. Silent shadow failures are very hard to spot
    // otherwise: an empty atlas looks identical to a fully lit scene.
    if (!m_shadowSetupLogged || glm::distance(m_loggedShadowLightDir, lightDir) > 0.05f) {
        m_shadowSetupLogged = true;
        m_loggedShadowLightDir = lightDir;
        DEMON_LOG_INFO("Shadows: sun dir ({:.2f}, {:.2f}, {:.2f}).", lightDir.x, lightDir.y, lightDir.z);
        DEMON_LOG_INFO("Shadows: cascade splits {:.1f} / {:.1f} / {:.1f} / {:.1f} m.",
                       m_sceneUbo.shadowSplits.x, m_sceneUbo.shadowSplits.y,
                       m_sceneUbo.shadowSplits.z, m_sceneUbo.shadowSplits.w);
        DEMON_LOG_INFO("Shadows: depth ranges {:.1f} / {:.1f} / {:.1f} / {:.1f} m.",
                       m_sceneUbo.shadowDepthRange.x, m_sceneUbo.shadowDepthRange.y,
                       m_sceneUbo.shadowDepthRange.z, m_sceneUbo.shadowDepthRange.w);
        DEMON_LOG_INFO("Shadows: texel size {:.3f} / {:.3f} / {:.3f} / {:.3f} m.",
                       m_sceneUbo.shadowTexelWorld.x, m_sceneUbo.shadowTexelWorld.y,
                       m_sceneUbo.shadowTexelWorld.z, m_sceneUbo.shadowTexelWorld.w);
    }
}

void Renderer::renderShadowAtlas(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_shadowPso || !m_settings.shadows.enabled || !m_shadowAtlas.isValid())
        return;

    bool hasShadowCasters = false;
    uint32_t casterCount = 0;
    for (const auto& dc : m_drawList) {
        if (dc.editorOverlay)
            continue;
        if (dc.mesh && dc.mesh->uploaded && dc.pipeline == DrawPipeline::Scene && !dc.alphaBlend && dc.castShadows) {
            hasShadowCasters = true;
            ++casterCount;
        }
    }
    if (casterCount != m_loggedShadowCasterCount) {
        m_loggedShadowCasterCount = casterCount;
        DEMON_LOG_INFO("Shadows: {} caster draw(s) in the cascade atlas.", static_cast<int>(casterCount));
    }
    if (!hasShadowCasters)
        return;

    m_shadowAtlas.transition(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto dsv = m_shadowAtlas.getDsv();
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_shadowPso.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const size_t frameSkinningBase = static_cast<size_t>(m_frameIndex) * static_cast<size_t>(k_maxAnimatedDraws + 1) * m_skinningCbStride;
    const D3D12_GPU_VIRTUAL_ADDRESS identitySkinningAddress =
        m_skinningCb ? (m_skinningCb->GetGPUVirtualAddress() + frameSkinningBase) : 0;

    const uint32_t tileSize = k_shadowAtlasSize / 2;
    for (uint32_t cascade = 0; cascade < k_shadowCascadeCount; ++cascade) {
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = static_cast<float>((cascade % 2) * tileSize);
        vp.TopLeftY = static_cast<float>((cascade / 2) * tileSize);
        vp.Width = static_cast<float>(tileSize);
        vp.Height = static_cast<float>(tileSize);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        D3D12_RECT sc{
            static_cast<LONG>((cascade % 2) * tileSize),
            static_cast<LONG>((cascade / 2) * tileSize),
            static_cast<LONG>(((cascade % 2) + 1) * tileSize),
            static_cast<LONG>(((cascade / 2) + 1) * tileSize)
        };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->SetGraphicsRootDescriptorTable(0, m_srvHeap.gpuHandle(getShadowCbvIndex(m_frameIndex, cascade)));

        for (const auto& dc : m_drawList) {
            if (dc.editorOverlay)
                continue;
            if (!dc.mesh || !dc.mesh->uploaded || dc.alphaBlend || !dc.castShadows || dc.pipeline != DrawPipeline::Scene)
                continue;

            cmd->IASetVertexBuffers(0, 1, &dc.mesh->vertexView);
            cmd->IASetIndexBuffer(&dc.mesh->indexView);

            D3D12_GPU_VIRTUAL_ADDRESS skinningAddress = identitySkinningAddress;
            if (dc.skinned && dc.skinMatrices && m_skinningCb && m_skinningCbMapped && m_skinningSlotCursor <= k_maxAnimatedDraws) {
                const size_t slotOffset = frameSkinningBase + static_cast<size_t>(m_skinningSlotCursor) * m_skinningCbStride;
                auto* dst = reinterpret_cast<glm::mat4*>(m_skinningCbMapped + slotOffset);
                for (uint32_t matrixIndex = 0; matrixIndex < kMaxSkinningMatrices; ++matrixIndex)
                    dst[matrixIndex] = glm::mat4(1.0f);
                const size_t matrixCount = std::min<size_t>(dc.skinMatrices->size(), kMaxSkinningMatrices);
                std::memcpy(dst, dc.skinMatrices->data(), matrixCount * sizeof(glm::mat4));
                skinningAddress = m_skinningCb->GetGPUVirtualAddress() + slotOffset;
                ++m_skinningSlotCursor;
            }
            cmd->SetGraphicsRootConstantBufferView(3, skinningAddress);

            struct PushConstants {
                glm::mat4 model;
                glm::vec4 albedo;
                glm::vec4 params;
                glm::vec4 flags;
                glm::vec4 skinning;
            } pc{};
            pc.model = dc.transform;
            pc.skinning = { dc.skinned ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            cmd->SetGraphicsRoot32BitConstants(4, 32, &pc, 0);

            const auto& subMeshes = dc.mesh->getSubMeshes();
            if (subMeshes.empty()) {
                cmd->DrawIndexedInstanced(static_cast<UINT>(dc.mesh->getIndices().size()), 1, 0, 0, 0);
            } else if (dc.subMeshIndex >= 0 && dc.subMeshIndex < static_cast<int32_t>(subMeshes.size())) {
                const auto& sm = subMeshes[static_cast<size_t>(dc.subMeshIndex)];
                if (sm.indexCount > 0)
                    cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
            } else if (dc.subMeshIndex < 0) {
                // -1 = intentional whole-mesh draw. An out-of-range index means a
                // mesh/scene mismatch — skip instead of drawing everything.
                for (const auto& sm : subMeshes) {
                    if (sm.indexCount == 0) continue;
                    cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
                }
            }
        }
    }

    m_shadowAtlas.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void Renderer::renderGBuffer(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_gbufferPso || !m_gbufferAlbedo.isValid() || !m_gbufferDepth.isValid())
        return;

    m_gbufferAlbedo.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_gbufferMaterial.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_gbufferNormal.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_gbufferEmissive.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_gbufferVelocity.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_gbufferDepth.transition(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = {
        m_gbufferAlbedo.getRtv(),
        m_gbufferMaterial.getRtv(),
        m_gbufferNormal.getRtv(),
        m_gbufferEmissive.getRtv(),
        m_gbufferVelocity.getRtv(),
    };
    auto dsv = m_gbufferDepth.getDsv();
    cmd->OMSetRenderTargets(5, rtvs, FALSE, &dsv);

    const float clearAlbedo[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float clearMaterial[4] = { 0.0f, 0.5f, 0.5f, 1.0f };
    const float clearZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    cmd->ClearRenderTargetView(rtvs[0], clearAlbedo, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[1], clearMaterial, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[2], clearZero, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[3], clearZero, 0, nullptr);
    cmd->ClearRenderTargetView(rtvs[4], clearZero, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(m_viewportWidth);
    vp.Height = static_cast<float>(m_viewportHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(m_viewportWidth), static_cast<LONG>(m_viewportHeight) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_gbufferPso.Get());
    cmd->SetGraphicsRootDescriptorTable(0, m_srvHeap.gpuHandle(getSceneCbvIndex(m_frameIndex)));
    cmd->SetGraphicsRootDescriptorTable(2, m_samplerHeap.gpuHandle(m_samplerWrapIndex));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const size_t frameSkinningBase =
        static_cast<size_t>(m_frameIndex) * static_cast<size_t>(k_maxAnimatedDraws + 1) * m_skinningCbStride;
    const D3D12_GPU_VIRTUAL_ADDRESS identitySkinningAddress =
        m_skinningCb ? (m_skinningCb->GetGPUVirtualAddress() + frameSkinningBase) : 0;

    D3D12_GPU_DESCRIPTOR_HANDLE lastMat = {};
    for (const auto& dc : m_drawList) {
        if (dc.editorOverlay)
            continue;
        if (!dc.mesh || !dc.mesh->uploaded || dc.alphaBlend || dc.pipeline != DrawPipeline::Scene)
            continue;

        uint32_t baseIndex = m_defaultMaterialTableIndex;
        if (dc.material && dc.material->srvTableIndex != UINT32_MAX)
            baseIndex = dc.material->srvTableIndex;
        D3D12_GPU_DESCRIPTOR_HANDLE matHandle = m_srvHeap.gpuHandle(baseIndex);
        if (matHandle.ptr != lastMat.ptr) {
            cmd->SetGraphicsRootDescriptorTable(1, matHandle);
            lastMat = matHandle;
        }

        cmd->IASetVertexBuffers(0, 1, &dc.mesh->vertexView);
        cmd->IASetIndexBuffer(&dc.mesh->indexView);

        D3D12_GPU_VIRTUAL_ADDRESS skinningAddress = identitySkinningAddress;
        if (dc.skinned && dc.skinMatrices && m_skinningCb && m_skinningCbMapped && m_skinningSlotCursor <= k_maxAnimatedDraws) {
            const size_t slotOffset = frameSkinningBase + static_cast<size_t>(m_skinningSlotCursor) * m_skinningCbStride;
            auto* dst = reinterpret_cast<glm::mat4*>(m_skinningCbMapped + slotOffset);
            for (uint32_t matrixIndex = 0; matrixIndex < kMaxSkinningMatrices; ++matrixIndex)
                dst[matrixIndex] = glm::mat4(1.0f);
            const size_t matrixCount = std::min<size_t>(dc.skinMatrices->size(), kMaxSkinningMatrices);
            std::memcpy(dst, dc.skinMatrices->data(), matrixCount * sizeof(glm::mat4));
            skinningAddress = m_skinningCb->GetGPUVirtualAddress() + slotOffset;
            ++m_skinningSlotCursor;
        }
        cmd->SetGraphicsRootConstantBufferView(3, skinningAddress);

        struct PushConstants {
            glm::mat4 model;
            glm::mat4 previousModel;
            glm::vec4 albedo;
            glm::vec4 params;
            glm::vec4 flags;
            glm::vec4 skinning;
        } pc{};
        pc.model = dc.transform;
        pc.previousModel = dc.previousTransform;
        pc.albedo = dc.albedo;
        pc.params = { dc.roughness, dc.metallic, dc.highlight, dc.ao };
        pc.flags = dc.texFlags;
        pc.skinning = glm::vec4(dc.skinned ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        cmd->SetGraphicsRoot32BitConstants(4, 48, &pc, 0);

        const auto& subMeshes = dc.mesh->getSubMeshes();
        if (subMeshes.empty()) {
            const uint32_t indexCount = static_cast<uint32_t>(dc.mesh->getIndices().size());
            cmd->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
            m_stats.gbufferDrawCalls++;
        } else if (dc.subMeshIndex >= 0 && dc.subMeshIndex < static_cast<int32_t>(subMeshes.size())) {
            const auto& sm = subMeshes[static_cast<size_t>(dc.subMeshIndex)];
            if (sm.indexCount > 0) {
                cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
                m_stats.gbufferDrawCalls++;
            }
        } else if (dc.subMeshIndex < 0) {
            // -1 = intentional whole-mesh draw. An out-of-range index means a
            // mesh/scene mismatch — skip instead of drawing everything, which
            // would repaint every submesh with this entity's material.
            for (const auto& sm : subMeshes) {
                if (sm.indexCount == 0)
                    continue;
                cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
                m_stats.gbufferDrawCalls++;
            }
        }
    }

    m_gbufferAlbedo.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_gbufferMaterial.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_gbufferNormal.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_gbufferEmissive.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_gbufferVelocity.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_gbufferDepth.transition(cmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void Renderer::runComputeStack(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_computeRootSignature || !m_gbufferDepth.isValid() ||
        m_gbufferTileCountX == 0 || m_gbufferTileCountY == 0 ||
        m_gbufferTileMaskUavIndex == UINT32_MAX)
        return;

    m_gbufferDepth.transition(cmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const uint32_t constants[4] = {
        m_viewportWidth,
        m_viewportHeight,
        m_gbufferTileCountX,
        m_gbufferTileCountY,
    };

    cmd->SetComputeRootSignature(m_computeRootSignature.Get());
    auto dispatchDepthTilePass = [&](ID3D12PipelineState* pso,
                                     ID3D12Resource* uavResource,
                                     uint32_t uavIndex) {
        if (!pso || !uavResource || uavIndex == UINT32_MAX)
            return;

        cmd->SetPipelineState(pso);
        cmd->SetComputeRoot32BitConstants(0, 4, constants, 0);
        cmd->SetComputeRootDescriptorTable(1, m_gbufferDepth.getSrvGpu());
        cmd->SetComputeRootDescriptorTable(2, m_srvHeap.gpuHandle(uavIndex));
        cmd->Dispatch(m_gbufferTileCountX, m_gbufferTileCountY, 1);
        auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(uavResource);
        cmd->ResourceBarrier(1, &barrier);
        m_stats.computeDispatches++;
        m_stats.computeTiles += m_gbufferTileCountX * m_gbufferTileCountY;
    };

    dispatchDepthTilePass(m_gbufferTileClassifyPso.Get(), m_gbufferTileMask.Get(), m_gbufferTileMaskUavIndex);
    dispatchDepthTilePass(m_gbufferSsaoPreparePso.Get(), m_ssaoTileMask.Get(), m_ssaoTileMaskUavIndex);
}

void Renderer::runPostLightingComputeStack(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_computeRootSignature || !m_hdrLuminanceReducePso ||
        !m_hdrLuminanceTiles || !m_viewportColor.isValid() ||
        m_hdrLuminanceTilesUavIndex == UINT32_MAX ||
        m_gbufferTileCountX == 0 || m_gbufferTileCountY == 0)
        return;

    m_viewportColor.transition(cmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const uint32_t constants[4] = {
        m_viewportWidth,
        m_viewportHeight,
        m_gbufferTileCountX,
        m_gbufferTileCountY,
    };

    cmd->SetComputeRootSignature(m_computeRootSignature.Get());
    cmd->SetPipelineState(m_hdrLuminanceReducePso.Get());
    cmd->SetComputeRoot32BitConstants(0, 4, constants, 0);
    cmd->SetComputeRootDescriptorTable(1, m_viewportColor.getSrvGpu());
    cmd->SetComputeRootDescriptorTable(2, m_srvHeap.gpuHandle(m_hdrLuminanceTilesUavIndex));
    cmd->Dispatch(m_gbufferTileCountX, m_gbufferTileCountY, 1);

    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_hdrLuminanceTiles.Get());
    cmd->ResourceBarrier(1, &barrier);

    m_stats.computeDispatches++;
    m_stats.computeTiles += m_gbufferTileCountX * m_gbufferTileCountY;
}

void Renderer::renderViewport(ID3D12GraphicsCommandList* cmd)
{
    if (m_viewportWidth == 0 || m_viewportHeight == 0 || !m_viewportColor.isValid())
        return;

    updateShadowCascades();
    if (m_sceneCbMapped) {
        std::memcpy(m_sceneCbMapped + (static_cast<size_t>(m_sceneCbStride) * m_frameIndex),
                    &m_sceneUbo, sizeof(SceneUBO));
    }
    renderShadowAtlas(cmd);
    renderGBuffer(cmd);
    runComputeStack(cmd);

    m_viewportColor.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_viewportVelocity.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_viewportDepth.transition(cmd, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { m_viewportColor.getRtv(), m_viewportVelocity.getRtv() };
    auto dsv = m_viewportDepth.getDsv();
    cmd->OMSetRenderTargets(2, rtvs, FALSE, &dsv);
    glm::vec3 clearColor = { 0.08f, 0.08f, 0.10f };
    if (m_skyEnabled && glm::length(m_skyAverage) > 0.0001f)
        clearColor = glm::clamp(m_skyAverage * std::max(m_skyIntensity, 1.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    if (m_fog.enabled) {
        const float fogBlend = m_skyEnabled
            ? glm::clamp(1.0f - std::exp(-m_fog.density * 24.0f), 0.0f, 0.85f)
            : 1.0f;
        clearColor = glm::mix(clearColor, m_fog.color, fogBlend);
    }
    const float clear[4] = { clearColor.r, clearColor.g, clearColor.b, 1.0f };
    cmd->ClearRenderTargetView(rtvs[0], clear, 0, nullptr);
    const float clearVelocity[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    cmd->ClearRenderTargetView(rtvs[1], clearVelocity, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(m_viewportWidth);
    vp.Height = static_cast<float>(m_viewportHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(m_viewportWidth), static_cast<LONG>(m_viewportHeight) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetGraphicsRootDescriptorTable(0, m_srvHeap.gpuHandle(getSceneCbvIndex(m_frameIndex)));
    cmd->SetGraphicsRootDescriptorTable(2, m_samplerHeap.gpuHandle(m_samplerWrapIndex));
    const size_t frameSkinningBase = static_cast<size_t>(m_frameIndex) * static_cast<size_t>(k_maxAnimatedDraws + 1) * m_skinningCbStride;
    const D3D12_GPU_VIRTUAL_ADDRESS identitySkinningAddress =
        m_skinningCb ? (m_skinningCb->GetGPUVirtualAddress() + frameSkinningBase) : 0;
    if (m_environmentSrvTableIndex == UINT32_MAX)
        m_environmentSrvTableIndex = m_srvHeap.allocate(2);
    if (!m_whiteTexture)  m_whiteTexture  = Texture::createWhite1x1(m_context, m_srvHeap);
    if (!m_blackTexture)  m_blackTexture  = Texture::createBlack1x1(m_context, m_srvHeap);
    auto device = m_context.getDevice();
    const auto shadowSrv = m_shadowAtlas.isValid() ? m_shadowAtlas.getSrvCpu() : m_whiteTexture->getSrvCpuHandle();
    const auto probeSrv = m_reflectionProbeTexture ? m_reflectionProbeTexture->getSrvCpuHandle() : m_blackTexture->getSrvCpuHandle();
    device->CopyDescriptorsSimple(1, m_srvHeap.cpuHandle(m_environmentSrvTableIndex + 0), shadowSrv,
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(1, m_srvHeap.cpuHandle(m_environmentSrvTableIndex + 1), probeSrv,
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cmd->SetGraphicsRootDescriptorTable(5, m_srvHeap.gpuHandle(m_environmentSrvTableIndex));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Skybox
    if (m_skyEnabled && m_skyPso) {
        if (!m_skySphere)
            m_skySphere = Mesh::createSphere(10.0f, 32, 32);
        if (!m_skySphere->uploaded)
            doUploadMesh(*m_skySphere);

        cmd->SetPipelineState(m_skyPso.Get());
        cmd->SetGraphicsRootDescriptorTable(1, m_srvHeap.gpuHandle(m_skySrvTableIndex));
        cmd->SetGraphicsRootDescriptorTable(2, m_samplerHeap.gpuHandle(m_samplerClampIndex));
        cmd->SetGraphicsRootConstantBufferView(3, identitySkinningAddress);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m_sceneUbo.cameraPos));
        struct PushConstants {
            glm::mat4 model;
            glm::mat4 previousModel;
            glm::vec4 albedo;
            glm::vec4 params;
            glm::vec4 flags;
            glm::vec4 skinning;
        } pc{};
        pc.model = model;
        pc.previousModel = model;
        pc.skinning = { 0.0f, 0.0f, 0.0f, 0.0f };
        cmd->SetGraphicsRoot32BitConstants(4, 48, &pc, 0);

        cmd->IASetVertexBuffers(0, 1, &m_skySphere->vertexView);
        cmd->IASetIndexBuffer(&m_skySphere->indexView);
        cmd->DrawIndexedInstanced(static_cast<UINT>(m_skySphere->getIndices().size()), 1, 0, 0, 0);
    }

    cmd->SetGraphicsRootDescriptorTable(2, m_samplerHeap.gpuHandle(m_samplerWrapIndex));

    std::sort(m_drawList.begin(), m_drawList.end(),
              [cameraPos = glm::vec3(m_sceneUbo.cameraPos)](const DrawCommand& a, const DrawCommand& b) {
                  if (a.editorOverlay != b.editorOverlay)
                      return !a.editorOverlay && b.editorOverlay;
                  if (a.alphaBlend != b.alphaBlend)
                      return !a.alphaBlend && b.alphaBlend;

                  if (!a.alphaBlend)
                      return false;

                  const float distanceA = glm::length2(glm::vec3(a.transform[3]) - cameraPos);
                  const float distanceB = glm::length2(glm::vec3(b.transform[3]) - cameraPos);
                  return distanceA > distanceB;
              });

    D3D12_GPU_DESCRIPTOR_HANDLE lastMat = {};
    bool lastAlphaBlend = false;
    bool lastEditorOverlay = false;
    DrawPipeline lastPipeline = DrawPipeline::Scene;
    bool pipelineBound = false;
    for (const auto& dc : m_drawList) {
        if (!dc.mesh || !dc.mesh->uploaded) continue;

        if (!pipelineBound || dc.alphaBlend != lastAlphaBlend ||
            dc.editorOverlay != lastEditorOverlay || dc.pipeline != lastPipeline) {
            ID3D12PipelineState* pipeline = nullptr;
            if (dc.editorOverlay && m_sceneOverlayPso)
                pipeline = m_sceneOverlayPso.Get();
            else if (dc.pipeline == DrawPipeline::Water && m_waterPso)
                pipeline = m_waterPso.Get();
            else
                pipeline = dc.alphaBlend ? m_sceneTransparentPso.Get() : m_scenePso.Get();

            cmd->SetPipelineState(pipeline);
            pipelineBound = true;
            lastAlphaBlend = dc.alphaBlend;
            lastEditorOverlay = dc.editorOverlay;
            lastPipeline = dc.pipeline;
            lastMat = {};
        }

        uint32_t baseIndex = m_defaultMaterialTableIndex;
        if (dc.material && dc.material->srvTableIndex != UINT32_MAX)
            baseIndex = dc.material->srvTableIndex;
        D3D12_GPU_DESCRIPTOR_HANDLE matHandle = m_srvHeap.gpuHandle(baseIndex);
        if (matHandle.ptr != lastMat.ptr) {
            cmd->SetGraphicsRootDescriptorTable(1, matHandle);
            lastMat = matHandle;
        }

        cmd->IASetVertexBuffers(0, 1, &dc.mesh->vertexView);
        cmd->IASetIndexBuffer(&dc.mesh->indexView);

        D3D12_GPU_VIRTUAL_ADDRESS skinningAddress = identitySkinningAddress;
        if (dc.skinned && dc.skinMatrices && m_skinningCb && m_skinningCbMapped && m_skinningSlotCursor <= k_maxAnimatedDraws) {
            const size_t slotOffset = frameSkinningBase + static_cast<size_t>(m_skinningSlotCursor) * m_skinningCbStride;
            auto* dst = reinterpret_cast<glm::mat4*>(m_skinningCbMapped + slotOffset);
            for (uint32_t matrixIndex = 0; matrixIndex < kMaxSkinningMatrices; ++matrixIndex)
                dst[matrixIndex] = glm::mat4(1.0f);
            const size_t matrixCount = std::min<size_t>(dc.skinMatrices->size(), kMaxSkinningMatrices);
            std::memcpy(dst, dc.skinMatrices->data(), matrixCount * sizeof(glm::mat4));
            skinningAddress = m_skinningCb->GetGPUVirtualAddress() + slotOffset;
            ++m_skinningSlotCursor;
        }
        cmd->SetGraphicsRootConstantBufferView(3, skinningAddress);

        struct PushConstants {
            glm::mat4 model;
            glm::mat4 previousModel;
            glm::vec4 albedo;
            glm::vec4 params;
            glm::vec4 flags;
            glm::vec4 skinning;
        } pc{};
        pc.model  = dc.transform;
        pc.previousModel = dc.previousTransform;
        pc.albedo = dc.albedo;
        pc.params = { dc.roughness, dc.metallic, dc.highlight, dc.ao };
        pc.flags  = dc.texFlags;
        pc.skinning = (dc.pipeline == DrawPipeline::Water)
            ? dc.customData
            : glm::vec4(dc.skinned ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        cmd->SetGraphicsRoot32BitConstants(4, 48, &pc, 0);

        const auto& subMeshes = dc.mesh->getSubMeshes();
        if (subMeshes.empty()) {
            uint32_t indexCount = static_cast<uint32_t>(dc.mesh->getIndices().size());
            cmd->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
            m_stats.drawCalls++;
            m_stats.indexCount  += indexCount;
            m_stats.vertexCount += static_cast<uint32_t>(dc.mesh->getVertices().size());
        } else if (dc.subMeshIndex >= 0 && dc.subMeshIndex < static_cast<int32_t>(subMeshes.size())) {
            const auto& sm = subMeshes[static_cast<size_t>(dc.subMeshIndex)];
            if (sm.indexCount > 0) {
                cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
                m_stats.drawCalls++;
                m_stats.indexCount += sm.indexCount;
            }
            m_stats.vertexCount += static_cast<uint32_t>(dc.mesh->getVertices().size());
        } else if (dc.subMeshIndex < 0) {
            // -1 = intentional whole-mesh draw; out-of-range would repaint
            // every submesh with this entity's material, so skip that case.
            for (const auto& sm : subMeshes) {
                if (sm.indexCount == 0) continue;
                cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexOffset, 0, 0);
                m_stats.drawCalls++;
                m_stats.indexCount += sm.indexCount;
            }
            m_stats.vertexCount += static_cast<uint32_t>(dc.mesh->getVertices().size());
        }
    }

    m_viewportColor.transition(cmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_viewportVelocity.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    runPostLightingComputeStack(cmd);
    m_viewportColor.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    const glm::vec3 currentCameraPosition = glm::vec3(m_sceneUbo.cameraPos);
    const glm::vec3 currentForward = glm::normalize(glm::vec3(-m_viewMatrix[0][2], -m_viewMatrix[1][2], -m_viewMatrix[2][2]));
    const bool cameraCut = m_temporalResetPending
        || !m_hasPreviousFrame
        || glm::distance(currentCameraPosition, m_previousCameraPosition) > 8.0f
        || glm::dot(currentForward, m_previousCameraForward) < 0.75f;

    PostProcessFrameData frameData{};
    frameData.inverseViewProjection = glm::inverse(m_viewProjection);
    frameData.previousViewProjection = m_previousViewProjection;
    frameData.cameraPosition = m_sceneUbo.cameraPos;
    frameData.lightDirection = m_sceneUbo.lightDir;
    frameData.lightColor = m_sceneUbo.lightColor;
    frameData.fogColorDensity = m_sceneUbo.fogColorDensity;
    frameData.fogParams = m_sceneUbo.fogParams;
    frameData.atmosphereParams0 = m_sceneUbo.atmosphereParams0;
    frameData.atmosphereParams1 = m_sceneUbo.atmosphereParams1;
    frameData.temporalJitter = glm::vec4(m_currentJitter, m_previousJitter);
    frameData.resetHistory = cameraCut;
    m_postProcessing.setFrameData(frameData);

    RHITexture* velocitySource = m_gbufferVelocity.isValid() ? &m_gbufferVelocity : &m_viewportVelocity;
    RHITexture* normalSource = m_gbufferNormal.isValid() ? &m_gbufferNormal : nullptr;
    RHITexture* albedoSource = m_gbufferAlbedo.isValid() ? &m_gbufferAlbedo : nullptr;
    RHITexture* output = m_postProcessing.execute(cmd, m_viewportColor, m_viewportDepth,
                                                  velocitySource, normalSource, albedoSource);
    if (output && output->isValid())
        m_viewportSrv = output->getSrvGpu();
    else
        m_viewportSrv = m_viewportColor.getSrvGpu();
    m_viewportOutput = (output && output->isValid()) ? output : &m_viewportColor;

    m_previousViewProjection = m_viewProjection;
    m_previousCameraPosition = currentCameraPosition;
    m_previousCameraForward = currentForward;
    m_previousJitter = m_currentJitter;
    m_hasPreviousFrame = true;
    m_temporalResetPending = false;
    m_previousPostProcessingEnabled = m_postProcessing.isEnabled();
    m_previousTaaEnabled = m_postProcessing.settings().taa.enabled;
    m_previousMotionBlurEnabled = m_postProcessing.settings().motionBlur.enabled;
    m_previousTaaJitterScale = m_postProcessing.settings().taa.jitterScale;
    for (const auto& dc : m_drawList) {
        if (dc.entityId != 0)
            m_previousTransforms[dc.entityId] = dc.transform;
    }
}

void Renderer::initSceneConstantBuffer()
{
    auto device = m_context.getDevice();
    m_sceneCbStride = (sizeof(SceneUBO) + 255) & ~255u;
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<uint64_t>(m_sceneCbStride) * k_maxFramesInFlight);
    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_sceneCb));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create scene constant buffer");

    CD3DX12_RANGE range(0, 0);
    m_sceneCb->Map(0, &range, reinterpret_cast<void**>(&m_sceneCbMapped));

    for (uint32_t i = 0; i < k_maxFramesInFlight; ++i) {
        m_sceneCbvIndex[i] = m_srvHeap.allocate(1);
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
        cbv.BufferLocation = m_sceneCb->GetGPUVirtualAddress() + static_cast<uint64_t>(m_sceneCbStride) * i;
        cbv.SizeInBytes = m_sceneCbStride;
        device->CreateConstantBufferView(&cbv, m_srvHeap.cpuHandle(m_sceneCbvIndex[i]));
    }
}

void Renderer::initSkinningConstantBuffer()
{
    auto device = m_context.getDevice();
    m_skinningCbStride = (sizeof(glm::mat4) * kMaxSkinningMatrices + 255) & ~255u;
    const uint64_t totalSlots = static_cast<uint64_t>(k_maxFramesInFlight) * static_cast<uint64_t>(k_maxAnimatedDraws + 1);
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<uint64_t>(m_skinningCbStride) * totalSlots);
    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skinningCb));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create skinning constant buffer");

    CD3DX12_RANGE range(0, 0);
    m_skinningCb->Map(0, &range, reinterpret_cast<void**>(&m_skinningCbMapped));
}

void Renderer::initShadowConstantBuffer()
{
    auto device = m_context.getDevice();
    m_shadowCbStride = (sizeof(ShadowSceneUBO) + 255) & ~255u;
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
        static_cast<uint64_t>(m_shadowCbStride) * k_maxFramesInFlight * k_shadowCascadeCount);
    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_shadowCb));
    DEMON_ASSERT(SUCCEEDED(hr), "Failed to create shadow constant buffer");

    CD3DX12_RANGE range(0, 0);
    m_shadowCb->Map(0, &range, reinterpret_cast<void**>(&m_shadowCbMapped));

    for (uint32_t frame = 0; frame < k_maxFramesInFlight; ++frame) {
        for (uint32_t cascade = 0; cascade < k_shadowCascadeCount; ++cascade) {
            const uint32_t idx = frame * k_shadowCascadeCount + cascade;
            m_shadowCbvIndex[idx] = m_srvHeap.allocate(1);

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
            cbv.BufferLocation = m_shadowCb->GetGPUVirtualAddress()
                               + static_cast<uint64_t>(m_shadowCbStride) * idx;
            cbv.SizeInBytes = m_shadowCbStride;
            device->CreateConstantBufferView(&cbv, m_srvHeap.cpuHandle(m_shadowCbvIndex[idx]));
        }
    }
}

uint32_t Renderer::getSceneCbvIndex(uint32_t frameIndex) const
{
    return m_sceneCbvIndex[frameIndex];
}

uint32_t Renderer::getShadowCbvIndex(uint32_t frameIndex, uint32_t cascadeIndex) const
{
    return m_shadowCbvIndex[frameIndex * k_shadowCascadeCount + cascadeIndex];
}

glm::vec2 Renderer::halton23(uint32_t index)
{
    auto halton = [](uint32_t i, uint32_t base) {
        float result = 0.0f;
        float factor = 1.0f;
        while (i > 0) {
            factor /= static_cast<float>(base);
            result += factor * static_cast<float>(i % base);
            i /= base;
        }
        return result;
    };

    return { halton(index, 2), halton(index, 3) };
}

void Renderer::extractFrustumPlanes(const glm::mat4& m)
{
    m_frustumPlanes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]); // left
    m_frustumPlanes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]); // right
    m_frustumPlanes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]); // bottom
    m_frustumPlanes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]); // top
    m_frustumPlanes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]); // near
    m_frustumPlanes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]); // far

    for (auto& p : m_frustumPlanes) {
        float invLen = 1.0f / glm::length(glm::vec3(p));
        p *= invLen;
    }
}

bool Renderer::isSphereInFrustum(const glm::vec3& center, float radius) const
{
    for (const auto& p : m_frustumPlanes) {
        if (glm::dot(glm::vec3(p), center) + p.w < -radius)
            return false;
    }
    return true;
}

void Renderer::destroyUploadedMeshes()
{
    // Meshes are owned outside the renderer. Procedural meshes can be replaced
    // while this tracking set still contains their old raw addresses, so
    // dereferencing here is unsafe during shutdown.
    m_uploadedMeshes.clear();
}

void Renderer::destroyUploadedMaterials()
{
    // Materials/textures are also externally owned; just drop renderer-side
    // tracking and heap indices on shutdown.
    m_uploadedMaterials.clear();
    m_defaultMaterialTableIndex = UINT32_MAX;
    m_skySrvTableIndex = UINT32_MAX;
    m_whiteTexture.reset();
    m_blackTexture.reset();
    m_normalTexture.reset();
    m_skyTexture.reset();
}

} // namespace Demon
