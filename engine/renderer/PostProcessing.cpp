// ============================================================================
//  DemonEngine::PostProcessing
// ============================================================================
#include "PostProcessing.h"
#include "DX12Context.h"
#include "DX12DescriptorHeap.h"
#include "core/Logger.h"
#include "core/PackageIO.h"
#include <directx/d3dx12.h>

namespace Demon {

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path)
{
    return PackageIO::readRuntimeBinary(path);
}

bool createPipelineState(ID3D12Device* device,
                         ID3D12RootSignature* rootSig,
                         const std::vector<uint8_t>& vs,
                         const std::vector<uint8_t>& ps,
                         DXGI_FORMAT format,
                         Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPso)
{
    if (!device || !rootSig || vs.empty() || ps.empty())
        return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.InputLayout = { nullptr, 0 };
    pso.pRootSignature = rootSig;
    pso.VS = { vs.data(), vs.size() };
    pso.PS = { ps.data(), ps.size() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = format;
    pso.SampleDesc.Count = 1;

    return SUCCEEDED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&outPso)));
}

} // namespace

void PostProcessing::init(DX12Context& ctx, DX12DescriptorHeap& srvHeap, DX12DescriptorHeap& rtvHeap, DX12DescriptorHeap& samplerHeap)
{
    m_ctx = &ctx;
    m_srvHeap = &srvHeap;
    m_rtvHeap = &rtvHeap;
    m_samplerHeap = &samplerHeap;
    ensureSamplers();
    createRootSignature();
    createPipelines(DXGI_FORMAT_R8G8B8A8_UNORM);
    if (m_srvTableIndex == UINT32_MAX)
        m_srvTableIndex = m_srvHeap->allocate(4 * kSrvTablesPerFrame);
}

void PostProcessing::shutdown()
{
    destroyPipelines();
    m_ping.destroy();
    m_pong.destroy();
    m_history.destroy();
    m_velocity.destroy();
    m_ssaoFactor.destroy();
    m_bloomExtract.destroy();
    m_bloomBlur.destroy();
    m_width = 0;
    m_height = 0;
    m_format = DXGI_FORMAT_UNKNOWN;
    m_pipelineFormat = DXGI_FORMAT_UNKNOWN;
    m_historyValid = false;
    m_featureMask = 0;
    m_wasEnabled = m_enabled;
    m_ctx = nullptr;
    m_srvHeap = nullptr;
    m_rtvHeap = nullptr;
    m_samplerHeap = nullptr;
}

void PostProcessing::resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;
    if (width == m_width && height == m_height)
        return;
    m_width = width;
    m_height = height;
    const DXGI_FORMAT format = (m_format == DXGI_FORMAT_UNKNOWN) ? DXGI_FORMAT_R8G8B8A8_UNORM : m_format;
    ensureTargets(width, height, format);
    m_historyValid = false;
}

RHITexture* PostProcessing::execute(ID3D12GraphicsCommandList* cmd, RHITexture& inputColor, RHITexture& inputDepth,
                                    RHITexture* inputVelocity, RHITexture* inputNormal, RHITexture* inputAlbedo)
{
    const bool atmosphereCompositeEnabled =
        m_settings.volumetric.enabled ||
        m_settings.volumetric.localEnabled ||
        m_settings.clouds.enabled ||
        m_settings.lensFlare.enabled;
    const uint32_t featureMask =
        (m_settings.taa.enabled ? (1u << 0) : 0u) |
        (m_settings.motionBlur.enabled ? (1u << 1) : 0u) |
        (atmosphereCompositeEnabled ? (1u << 2) : 0u) |
        (m_settings.bloom.enabled ? (1u << 3) : 0u) |
        (m_settings.aces.enabled ? (1u << 4) : 0u) |
        (m_settings.dof.enabled ? (1u << 5) : 0u) |
        (m_settings.ssao.enabled ? (1u << 6) : 0u) |
        (m_settings.ssgi.enabled ? (1u << 7) : 0u);
    if (featureMask != m_featureMask || m_enabled != m_wasEnabled) {
        m_historyValid = false;
        m_featureMask = featureMask;
        m_wasEnabled = m_enabled;
    }

    m_srvTableCursor = 0;

    if (!m_enabled || !cmd || inputColor.getWidth() == 0 || inputColor.getHeight() == 0)
        return &inputColor;

    ensureTargets(inputColor.getWidth(), inputColor.getHeight(), inputColor.getFormat());

    if (m_frameData.resetHistory || !m_settings.taa.enabled)
        m_historyValid = false;

    RHITexture* velocitySource = inputVelocity;
    if (!velocitySource && (m_settings.taa.enabled || m_settings.motionBlur.enabled)) {
        if (m_velocityPipeline.pso) {
            PostProcessConstants velocityConstants{};
            velocityConstants.params3 = glm::vec4(1.0f / static_cast<float>(inputColor.getWidth()),
                                                  1.0f / static_cast<float>(inputColor.getHeight()),
                                                  0.0f, 0.0f);
            velocityConstants.matrix0 = m_frameData.inverseViewProjection;
            velocityConstants.matrix1 = m_frameData.previousViewProjection;
            passFullscreen(cmd, { &inputColor, &inputDepth, nullptr, nullptr }, m_velocity, m_velocityPipeline, velocityConstants);
            velocitySource = &m_velocity;
        } else {
            warnOnce(m_warnVelocity, "Velocity");
        }
    }

    RHITexture* current = &inputColor;
    RHITexture* next = &m_ping;
    auto advanceTarget = [&]() {
        current = next;
        next = (next == &m_ping) ? &m_pong : &m_ping;
    };

    if (m_settings.ssgi.enabled) {
        const bool ssgiInputsValid =
            inputNormal && inputNormal->isValid() &&
            inputAlbedo && inputAlbedo->isValid();
        if (m_ssgiPipeline.pso && ssgiInputsValid) {
            PostProcessConstants constants{};
            constants.params0 = glm::vec4(m_settings.ssgi.radius,
                                          m_settings.ssgi.intensity,
                                          m_settings.ssgi.thickness,
                                          m_settings.ssgi.saturation);
            constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                          1.0f / static_cast<float>(current->getHeight()),
                                          0.0f, 0.0f);
            constants.matrix0 = m_frameData.inverseViewProjection;
            constants.frame0 = m_frameData.cameraPosition;
            passFullscreen(cmd, { current, &inputDepth, inputAlbedo, inputNormal }, *next, m_ssgiPipeline, constants);
            advanceTarget();
        } else {
            warnOnce(m_warnSSGI, "SSGI");
        }
    }

    if (m_settings.ssao.enabled) {
        if (m_ssaoPipeline.pso && m_ssaoApplyPipeline.pso) {
            PostProcessConstants constants{};
            constants.params0 = glm::vec4(m_settings.ssao.radius,
                                          m_settings.ssao.power,
                                          m_settings.ssao.bias,
                                          inputNormal && inputNormal->isValid() ? 1.0f : 0.0f);
            constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                          1.0f / static_cast<float>(current->getHeight()),
                                          0.0f, 0.0f);
            constants.matrix0 = m_frameData.inverseViewProjection;
            constants.frame0 = m_frameData.cameraPosition;
            passFullscreen(cmd, { current, &inputDepth, nullptr, inputNormal }, m_ssaoFactor, m_ssaoPipeline, constants);
            passFullscreen(cmd, { current, &inputDepth, nullptr, &m_ssaoFactor }, *next, m_ssaoApplyPipeline, constants);
            advanceTarget();
        } else {
            warnOnce(m_warnSSAO, "SSAO");
        }
    }

    if (atmosphereCompositeEnabled) {
        if (m_volumetricPipeline.pso) {
            PostProcessConstants constants{};
            constants.params0 = glm::vec4(m_settings.volumetric.density,
                                          m_settings.volumetric.intensity,
                                          m_settings.volumetric.anisotropy,
                                          m_settings.volumetric.startDistance);
            constants.params1 = glm::vec4(m_settings.volumetric.color,
                                          m_settings.volumetric.enabled ? m_settings.volumetric.density : 0.0f);
            constants.params2 = glm::vec4(m_settings.volumetric.height,
                                          m_settings.volumetric.heightFalloff,
                                          m_settings.volumetric.localEnabled ? 1.0f : 0.0f,
                                          m_settings.volumetric.maxOpacity);
            constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                          1.0f / static_cast<float>(current->getHeight()),
                                          m_settings.clouds.enabled ? 1.0f : 0.0f,
                                          m_settings.lensFlare.enabled ? 1.0f : 0.0f);
            constants.matrix0 = m_frameData.inverseViewProjection;
            constants.matrix1[0] = glm::vec4(m_settings.clouds.coverage,
                                             m_settings.clouds.density,
                                             m_settings.clouds.altitude,
                                             m_settings.clouds.thickness);
            constants.matrix1[1] = glm::vec4(m_settings.clouds.scale,
                                             m_settings.clouds.speed,
                                             m_settings.clouds.darkness,
                                             m_settings.lensFlare.intensity);
            if (m_settings.volumetric.localEnabled) {
                constants.matrix1[2] = glm::vec4(m_settings.volumetric.localExtents,
                                                 m_settings.volumetric.localDensity);
            } else {
                constants.matrix1[2] = glm::vec4(m_settings.lensFlare.threshold,
                                                 m_settings.lensFlare.haloWidth,
                                                 m_settings.lensFlare.ghostSpacing,
                                                 m_settings.lensFlare.dirtIntensity);
            }
            const glm::vec3 packedTint = (m_settings.volumetric.localEnabled && !m_settings.clouds.enabled)
                ? m_settings.volumetric.localColor
                : m_settings.clouds.tint;
            constants.matrix1[3] = glm::vec4(packedTint, m_frameData.cameraPosition.w);
            constants.frame0 = glm::vec4(m_frameData.cameraPosition.x,
                                         m_frameData.cameraPosition.y,
                                         m_frameData.cameraPosition.z,
                                         m_settings.volumetric.localCenter.x);
            constants.frame1 = glm::vec4(m_frameData.lightDirection.x,
                                         m_frameData.lightDirection.y,
                                         m_frameData.lightDirection.z,
                                         m_settings.volumetric.localCenter.y);
            constants.frame2 = glm::vec4(m_frameData.lightColor.x,
                                         m_frameData.lightColor.y,
                                         m_frameData.lightColor.z,
                                         m_settings.volumetric.localCenter.z);
            passFullscreen(cmd, { current, &inputDepth, nullptr, nullptr }, *next, m_volumetricPipeline, constants);
            advanceTarget();
        } else {
            warnOnce(m_warnVolumetric, "Volumetric");
        }
    }

    if (m_settings.taa.enabled) {
        if (m_taaPipeline.pso && velocitySource) {
            if (m_historyValid) {
                PostProcessConstants constants{};
                constants.params0 = glm::vec4(m_settings.taa.feedback,
                                              m_settings.taa.sharpness,
                                              m_settings.taa.motionRejection,
                                              0.0f);
                constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                              1.0f / static_cast<float>(current->getHeight()),
                                              0.0f, 0.0f);
                // History is captured immediately after TAA so later blur passes never feed back.
                passFullscreen(cmd, { current, &inputDepth, &m_history, velocitySource ? velocitySource : &m_velocity }, *next, m_taaPipeline, constants);
                advanceTarget();
            }
            copyToHistory(cmd, *current);
        } else if (!m_taaPipeline.pso) {
            warnOnce(m_warnTAA, "TAA");
            copyToHistory(cmd, *current);
        } else {
            copyToHistory(cmd, *current);
        }
    }

    if (m_settings.motionBlur.enabled) {
        if (m_motionBlurPipeline.pso && velocitySource) {
            PostProcessConstants constants{};
            constants.params0 = glm::vec4(m_settings.motionBlur.shutterScale,
                                          m_settings.motionBlur.maxBlurPixels,
                                          static_cast<float>(m_settings.motionBlur.sampleCount),
                                          m_settings.motionBlur.depthThreshold);
            constants.params1 = m_frameData.temporalJitter;
            constants.params2 = glm::vec4(0.0f, 0.0f, 0.0f, m_frameData.resetHistory ? 1.0f : 0.0f);
            constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                          1.0f / static_cast<float>(current->getHeight()),
                                          0.0f, 0.0f);
            passFullscreen(cmd, { current, &inputDepth, nullptr, velocitySource }, *next, m_motionBlurPipeline, constants);
            advanceTarget();
        } else {
            warnOnce(m_warnMotionBlur, "Motion Blur");
        }
    }

    if (m_settings.dof.enabled) {
        if (m_dofPipeline.pso) {
            PostProcessConstants constants{};
            constants.params1 = glm::vec4(m_settings.dof.focusDistance,
                                          m_settings.dof.focusRange,
                                          m_settings.dof.blurStrength,
                                          0.0f);
            constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                          1.0f / static_cast<float>(current->getHeight()),
                                          0.0f, 0.0f);
            passFullscreen(cmd, { current, &inputDepth, nullptr, nullptr }, *next, m_dofPipeline, constants);
            advanceTarget();
        } else {
            warnOnce(m_warnDOF, "DOF");
        }
    }

    if (m_settings.bloom.enabled) {
        if (m_bloomExtractPipeline.pso && m_bloomBlurPipeline.pso && m_bloomCombinePipeline.pso
            && m_bloomExtract.isValid() && m_bloomBlur.isValid()) {
            PostProcessConstants extractConstants{};
            extractConstants.params0 = glm::vec4(m_settings.bloom.threshold,
                                                 m_settings.bloom.softKnee,
                                                 0.0f, 0.0f);
            extractConstants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                                 1.0f / static_cast<float>(current->getHeight()),
                                                 0.0f, 0.0f);
            passFullscreen(cmd, { current, nullptr, nullptr, nullptr }, m_bloomExtract, m_bloomExtractPipeline, extractConstants);

            PostProcessConstants blurConstants{};
            blurConstants.params0 = glm::vec4(m_settings.bloom.radius, 1.0f, 0.0f, 0.0f);
            blurConstants.params3 = glm::vec4(1.0f / static_cast<float>(m_bloomExtract.getWidth()),
                                              1.0f / static_cast<float>(m_bloomExtract.getHeight()),
                                              0.0f, 0.0f);
            passFullscreen(cmd, { &m_bloomExtract, nullptr, nullptr, nullptr }, m_bloomBlur, m_bloomBlurPipeline, blurConstants);

            blurConstants.params0.y = 0.0f;
            blurConstants.params3 = glm::vec4(1.0f / static_cast<float>(m_bloomBlur.getWidth()),
                                              1.0f / static_cast<float>(m_bloomBlur.getHeight()),
                                              0.0f, 0.0f);
            passFullscreen(cmd, { &m_bloomBlur, nullptr, nullptr, nullptr }, m_bloomExtract, m_bloomBlurPipeline, blurConstants);

            PostProcessConstants combineConstants{};
            combineConstants.params0 = glm::vec4(m_settings.bloom.intensity,
                                                 0.0f, 0.0f, 0.0f);
            combineConstants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                                 1.0f / static_cast<float>(current->getHeight()),
                                                 0.0f, 0.0f);
            passFullscreen(cmd, { current, nullptr, nullptr, &m_bloomExtract }, *next, m_bloomCombinePipeline, combineConstants);
            advanceTarget();
        } else {
            warnOnce(m_warnBloom, "Bloom");
        }
    }

    if (m_settings.aces.enabled) {
        if (m_tonemapPipeline.pso) {
            PostProcessConstants constants{};
            constants.params0 = glm::vec4(static_cast<float>(m_settings.aces.mode),
                                          0.0f,
                                          0.0f,
                                          m_settings.aces.exposure);
            constants.params1 = glm::vec4(m_settings.aces.contrast,
                                          m_settings.aces.saturation,
                                          m_settings.aces.vignette,
                                          m_settings.aces.grain);
            constants.params2 = glm::vec4(m_settings.aces.temperature,
                                          m_settings.aces.tint,
                                          m_settings.aces.lift,
                                          m_settings.aces.gamma);
            constants.params3 = glm::vec4(1.0f / static_cast<float>(current->getWidth()),
                                          1.0f / static_cast<float>(current->getHeight()),
                                          0.0f, 0.0f);
            constants.frame0 = glm::vec4(m_settings.aces.gain, 0.0f, 0.0f, 0.0f);
            passFullscreen(cmd, { current, nullptr, nullptr, nullptr }, *next, m_tonemapPipeline, constants);
            advanceTarget();
        } else {
            warnOnce(m_warnTonemap, "ToneMap");
        }
    }

    return current;
}

void PostProcessing::ensureTargets(uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    if (!m_ctx || !m_rtvHeap || !m_srvHeap)
        return;
    if (m_ping.getWidth() == width
        && m_ping.getHeight() == height
        && m_format == format
        && m_ping.isValid()
        && m_bloomExtract.isValid()
        && m_bloomBlur.isValid())
        return;

    auto device = m_ctx->getDevice();

    RHITextureDesc colorDesc{};
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = format;
    colorDesc.mipLevels = 1;
    colorDesc.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    colorDesc.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    colorDesc.createSRV = true;
    colorDesc.createRTV = true;
    colorDesc.hasClearValue = true;
    colorDesc.clearValue.Format = format;
    colorDesc.clearValue.Color[0] = 0.0f;
    colorDesc.clearValue.Color[1] = 0.0f;
    colorDesc.clearValue.Color[2] = 0.0f;
    colorDesc.clearValue.Color[3] = 1.0f;

    m_ping.create(device, colorDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_Ping");
    m_pong.create(device, colorDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_Pong");
    m_history.create(device, colorDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_History");

    RHITextureDesc velocityDesc = colorDesc;
    velocityDesc.format = DXGI_FORMAT_R16G16_FLOAT;
    velocityDesc.clearValue.Format = velocityDesc.format;
    m_velocity.create(device, velocityDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_Velocity");

    RHITextureDesc aoDesc = colorDesc;
    aoDesc.format = DXGI_FORMAT_R8_UNORM;
    aoDesc.clearValue.Format = aoDesc.format;
    aoDesc.clearValue.Color[0] = 1.0f;
    aoDesc.clearValue.Color[1] = 0.0f;
    aoDesc.clearValue.Color[2] = 0.0f;
    aoDesc.clearValue.Color[3] = 1.0f;
    m_ssaoFactor.create(device, aoDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_SSAOFactor");

    RHITextureDesc halfColorDesc = colorDesc;
    halfColorDesc.width = std::max(1u, width / 2u);
    halfColorDesc.height = std::max(1u, height / 2u);
    m_bloomExtract.create(device, halfColorDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_BloomExtract");
    m_bloomBlur.create(device, halfColorDesc, m_rtvHeap, nullptr, m_srvHeap, "PostProcess_BloomBlur");

    m_width = width;
    m_height = height;
    m_format = format;
    m_historyValid = false;

    if (!m_rootSignature)
        createRootSignature();
    if (m_pipelineFormat != format)
        createPipelines(format);
}

void PostProcessing::ensureSamplers()
{
    if (!m_ctx || !m_samplerHeap)
        return;

    auto device = m_ctx->getDevice();

    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    m_linearClamp.create(device, *m_samplerHeap, sampler);

    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    m_linearWrap.create(device, *m_samplerHeap, sampler);
}

void PostProcessing::createRootSignature()
{
    if (!m_ctx || m_rootSignature)
        return;

    CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    CD3DX12_DESCRIPTOR_RANGE samplerRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[2].InitAsConstants(60, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC desc{};
    desc.Init(_countof(params), params, 0, nullptr,
              D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> sig;
    Microsoft::WRL::ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    DEMON_ASSERT(SUCCEEDED(hr), "PostProcessing: failed to serialize root signature");
    hr = m_ctx->getDevice()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                                 IID_PPV_ARGS(&m_rootSignature));
    DEMON_ASSERT(SUCCEEDED(hr), "PostProcessing: failed to create root signature");
}

void PostProcessing::createPipelines(DXGI_FORMAT format)
{
    if (!m_ctx)
        return;

    destroyPipelines();
    createRootSignature();

    const auto vs = readFileBytes("assets/shaders/dx12/post_fullscreen_vs.cso");
    if (vs.empty()) {
        DEMON_LOG_ERROR("PostProcessing: missing fullscreen VS shader (post_fullscreen_vs.cso).");
        return;
    }

    auto device = m_ctx->getDevice();

    auto makePSO = [&](Pipeline& pipeline, const char* psPath, DXGI_FORMAT outFormat) {
        const auto ps = readFileBytes(psPath);
        if (ps.empty()) {
            DEMON_LOG_ERROR("PostProcessing: missing shader '{}'.", psPath);
            return;
        }
        if (!createPipelineState(device, m_rootSignature.Get(), vs, ps, outFormat, pipeline.pso))
            DEMON_LOG_ERROR("PostProcessing: failed to create PSO for {}.", pipeline.name ? pipeline.name : "pass");
    };

    makePSO(m_copyPipeline,       "assets/shaders/dx12/post_copy_ps.cso", format);
    // The scene/post-process chain is HDR, while the Qt-hosted DXGI swapchain
    // is SDR. D3D12 pipeline render-target formats must match the bound RTV.
    makePSO(m_presentPipeline,    "assets/shaders/dx12/post_copy_ps.cso", DXGI_FORMAT_R8G8B8A8_UNORM);
    makePSO(m_velocityPipeline,   "assets/shaders/dx12/post_velocity_ps.cso", DXGI_FORMAT_R16G16_FLOAT);
    makePSO(m_taaPipeline,        "assets/shaders/dx12/post_taa_ps.cso", format);
    makePSO(m_motionBlurPipeline, "assets/shaders/dx12/post_motion_blur_ps.cso", format);
    makePSO(m_volumetricPipeline, "assets/shaders/dx12/post_volumetric_ps.cso", format);
    makePSO(m_bloomExtractPipeline, "assets/shaders/dx12/post_bloom_extract_ps.cso", format);
    makePSO(m_bloomBlurPipeline,    "assets/shaders/dx12/post_bloom_blur_ps.cso", format);
    makePSO(m_bloomCombinePipeline, "assets/shaders/dx12/post_bloom_combine_ps.cso", format);
    makePSO(m_tonemapPipeline,      "assets/shaders/dx12/post_tonemap_ps.cso", format);
    makePSO(m_dofPipeline,        "assets/shaders/dx12/post_dof_ps.cso", format);
    makePSO(m_ssgiPipeline,       "assets/shaders/dx12/post_ssgi_ps.cso", format);
    makePSO(m_ssaoPipeline,       "assets/shaders/dx12/post_ssao_ps.cso", DXGI_FORMAT_R8_UNORM);
    makePSO(m_ssaoApplyPipeline,  "assets/shaders/dx12/post_ssao_apply_ps.cso", format);

    m_pipelineFormat = format;
}

void PostProcessing::reloadPipelines()
{
    if (!m_ctx || m_pipelineFormat == DXGI_FORMAT_UNKNOWN)
        return;
    // createPipelines() re-reads every .cso from disk and self-destroys the old
    // PSO set first, so this cleanly hot-swaps the whole post chain.
    createPipelines(m_pipelineFormat);
}

void PostProcessing::destroyPipelines()
{
    m_copyPipeline.pso.Reset();
    m_presentPipeline.pso.Reset();
    m_velocityPipeline.pso.Reset();
    m_taaPipeline.pso.Reset();
    m_motionBlurPipeline.pso.Reset();
    m_volumetricPipeline.pso.Reset();
    m_bloomExtractPipeline.pso.Reset();
    m_bloomBlurPipeline.pso.Reset();
    m_bloomCombinePipeline.pso.Reset();
    m_tonemapPipeline.pso.Reset();
    m_dofPipeline.pso.Reset();
    m_ssgiPipeline.pso.Reset();
    m_ssaoPipeline.pso.Reset();
    m_ssaoApplyPipeline.pso.Reset();
    m_rootSignature.Reset();
}

void PostProcessing::passFullscreen(ID3D12GraphicsCommandList* cmd, const PassInputs& inputs, RHITexture& dst,
                                    Pipeline& pipeline, const PostProcessConstants& constants)
{
    if (!cmd || !pipeline.pso || !m_rootSignature || !m_srvHeap || !m_samplerHeap)
        return;

    if (m_srvTableIndex == UINT32_MAX)
        m_srvTableIndex = m_srvHeap->allocate(4 * kSrvTablesPerFrame);
    if (m_srvTableCursor >= kSrvTablesPerFrame) {
        DEMON_LOG_WARN("PostProcessing: SRV table ring exhausted; skipping {} pass.", pipeline.name ? pipeline.name : "post-process");
        return;
    }

    const uint32_t tableIndex = m_srvTableIndex + (m_srvTableCursor++ * 4);

    RHITexture* color = inputs.color ? inputs.color : &dst;
    RHITexture* depth = inputs.depth ? inputs.depth : color;
    RHITexture* history = inputs.history ? inputs.history : color;
    RHITexture* auxiliary = inputs.auxiliary ? inputs.auxiliary : depth;

    auto device = m_ctx->getDevice();
    device->CopyDescriptorsSimple(1, m_srvHeap->cpuHandle(tableIndex + 0),
                                  color->getSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(1, m_srvHeap->cpuHandle(tableIndex + 1),
                                  depth->getSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(1, m_srvHeap->cpuHandle(tableIndex + 2),
                                  history->getSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(1, m_srvHeap->cpuHandle(tableIndex + 3),
                                  auxiliary->getSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    color->transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    depth->transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    history->transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    auxiliary->transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dst.transition(cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto rtv = dst.getRtv();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);

    D3D12_VIEWPORT vp{};
    vp.Width = static_cast<float>(dst.getWidth());
    vp.Height = static_cast<float>(dst.getHeight());
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{ 0, 0, static_cast<LONG>(dst.getWidth()), static_cast<LONG>(dst.getHeight()) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(pipeline.pso.Get());
    cmd->SetGraphicsRootDescriptorTable(0, m_srvHeap->gpuHandle(tableIndex));
    cmd->SetGraphicsRootDescriptorTable(1, m_linearClamp.getGpuHandle());
    cmd->SetGraphicsRoot32BitConstants(2, 60, &constants, 0);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    dst.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void PostProcessing::present(ID3D12GraphicsCommandList* cmd, RHITexture& input,
                             D3D12_CPU_DESCRIPTOR_HANDLE targetRtv,
                             uint32_t width, uint32_t height)
{
    if (!cmd || !input.isValid() || !m_presentPipeline.pso || !m_rootSignature ||
        !m_srvHeap || !m_samplerHeap || width == 0 || height == 0)
        return;

    if (m_srvTableIndex == UINT32_MAX)
        m_srvTableIndex = m_srvHeap->allocate(4 * kSrvTablesPerFrame);
    if (m_srvTableCursor >= kSrvTablesPerFrame)
        return;

    const uint32_t tableIndex = m_srvTableIndex + (m_srvTableCursor++ * 4);
    auto device = m_ctx->getDevice();
    for (uint32_t i = 0; i < 4; ++i) {
        device->CopyDescriptorsSimple(1, m_srvHeap->cpuHandle(tableIndex + i),
                                      input.getSrvCpu(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    input.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmd->OMSetRenderTargets(1, &targetRtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_presentPipeline.pso.Get());
    cmd->SetGraphicsRootDescriptorTable(0, m_srvHeap->gpuHandle(tableIndex));
    cmd->SetGraphicsRootDescriptorTable(1, m_linearClamp.getGpuHandle());
    PostProcessConstants constants{};
    cmd->SetGraphicsRoot32BitConstants(2, 60, &constants, 0);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

void PostProcessing::copyToHistory(ID3D12GraphicsCommandList* cmd, RHITexture& src)
{
    if (!cmd || !m_history.isValid() || !src.isValid())
        return;

    src.transition(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_history.transition(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(m_history.getResource(), src.getResource());
    m_history.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    src.transition(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_historyValid = true;
}

void PostProcessing::warnOnce(bool& flag, const char* effectName)
{
    if (flag)
        return;
    flag = true;
    DEMON_LOG_WARN("PostProcessing: {} pass skipped (shader missing or failed to compile).", effectName);
}

} // namespace Demon
