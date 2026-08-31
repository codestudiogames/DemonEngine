#pragma once
// ============================================================================
//  DemonEngine::PostProcessing
//  Post-processing framework with effect settings and pass scheduling.
// ============================================================================
#include "core/DemonPCH.h"
#include "RHITexture.h"
#include "RHISampler.h"

namespace Demon {

class DX12Context;
class DX12DescriptorHeap;

struct TAASettings {
    bool  enabled = true;
    float jitterScale = 1.0f;
    float feedback = 0.92f;
    float sharpness = 0.12f;
    float motionRejection = 0.7f;
};

struct MotionBlurSettings {
    bool  enabled = true;
    float shutterScale = 0.85f;
    float maxBlurPixels = 18.0f;
    float depthThreshold = 0.0025f;
    int   sampleCount = 8;
};

struct BloomSettings {
    bool  enabled = true;
    float threshold = 1.15f;
    float intensity = 0.30f;
    float radius = 1.35f;
    float softKnee = 0.5f;
};

enum class TonemapOperator : uint32_t {
    ACESFilm = 0,
    Reinhard = 1,
};

struct TonemapSettings {
    bool  enabled = true;
    TonemapOperator mode = TonemapOperator::ACESFilm;
    float exposure = 1.15f;
    float contrast = 1.06f;
    float saturation = 1.05f;
    float vignette = 0.18f;
    float grain = 0.025f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float lift = 0.0f;
    float gamma = 1.0f;
    float gain = 1.0f;
};

struct DOFSettings {
    bool  enabled = false;
    float focusDistance = 8.0f;
    float focusRange = 4.0f;
    float blurStrength = 1.0f;
};

struct SSGISettings {
    bool  enabled = true;
    float radius = 3.25f;
    float intensity = 1.28f;
    float thickness = 0.48f;
    float saturation = 1.34f;
};

struct SSAOSettings {
    bool  enabled = true;
    float radius = 1.35f;
    float power = 1.45f;
    float bias = 0.035f;
};

struct VolumetricSettings {
    bool  enabled = false;
    glm::vec3 color = {0.62f, 0.68f, 0.78f};
    float density = 0.008f;
    float intensity = 0.34f;
    float anisotropy = 0.32f;
    float startDistance = 8.0f;
    float height = 0.0f;
    float heightFalloff = 0.18f;
    float maxOpacity = 0.28f;
    bool localEnabled = false;
    glm::vec3 localCenter = {0.0f, 2.0f, 0.0f};
    glm::vec3 localExtents = {6.0f, 3.0f, 6.0f};
    glm::vec3 localColor = {0.58f, 0.64f, 0.74f};
    float localDensity = 0.045f;
    float localIntensity = 0.55f;
    float localEdgeSoftness = 0.65f;
};

struct VolumetricCloudSettings {
    bool enabled = false;
    float coverage = 0.48f;
    float density = 0.52f;
    float altitude = 85.0f;
    float thickness = 45.0f;
    float scale = 0.75f;
    float speed = 0.035f;
    float darkness = 0.22f;
    glm::vec3 tint = {1.0f, 0.98f, 0.92f};
};

struct LensFlareSettings {
    bool enabled = false;
    float intensity = 0.28f;
    float threshold = 0.82f;
    float haloWidth = 0.45f;
    float ghostSpacing = 0.36f;
    float dirtIntensity = 0.0f;
    glm::vec3 tint = {1.0f, 0.86f, 0.62f};
};

struct PostProcessSettings {
    TAASettings    taa{};
    MotionBlurSettings motionBlur{};
    VolumetricSettings volumetric{};
    VolumetricCloudSettings clouds{};
    LensFlareSettings lensFlare{};
    BloomSettings  bloom{};
    TonemapSettings aces{};
    DOFSettings    dof{};
    SSGISettings   ssgi{};
    SSAOSettings   ssao{};
};

struct PostProcessFrameData {
    glm::mat4 inverseViewProjection{1.0f};
    glm::mat4 previousViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 lightDirection{0.4f, -1.0f, 0.3f, 1.0f};
    glm::vec4 lightColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 fogColorDensity{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 fogParams{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 atmosphereParams0{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 atmosphereParams1{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 temporalJitter{0.0f, 0.0f, 0.0f, 0.0f};
    bool resetHistory = true;
};

class PostProcessing {
public:
    PostProcessing() = default;
    ~PostProcessing() { shutdown(); }

    // Main-thread-only: create post-processing resources.
    void init(DX12Context& ctx, DX12DescriptorHeap& srvHeap, DX12DescriptorHeap& rtvHeap, DX12DescriptorHeap& samplerHeap);
    // Main-thread-only: release resources.
    void shutdown();
    // Main-thread-only: resize render targets.
    void resize(uint32_t width, uint32_t height);

    // Main-thread-only: update effect settings.
    void setSettings(const PostProcessSettings& settings) { m_settings = settings; }
    [[nodiscard]] PostProcessSettings& settings() { return m_settings; }
    [[nodiscard]] const PostProcessSettings& settings() const { return m_settings; }

    // Render-thread-only: records post-processing passes.
    RHITexture* execute(ID3D12GraphicsCommandList* cmd, RHITexture& inputColor, RHITexture& inputDepth,
                        RHITexture* inputVelocity = nullptr, RHITexture* inputNormal = nullptr,
                        RHITexture* inputAlbedo = nullptr);
    void present(ID3D12GraphicsCommandList* cmd, RHITexture& input,
                 D3D12_CPU_DESCRIPTOR_HANDLE targetRtv, uint32_t width, uint32_t height);

    void setEnabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    // Main-thread-only: rebuild every post-processing PSO from the freshly
    // compiled shader bytecode on disk (shader hot-reload). No-op before init().
    void reloadPipelines();

private:
    struct Pipeline {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        const char* name = nullptr;
    };

    struct PostProcessConstants {
        glm::vec4 params0{0.0f};
        glm::vec4 params1{0.0f};
        glm::vec4 params2{0.0f};
        glm::vec4 params3{0.0f};
        glm::mat4 matrix0{1.0f};
        glm::mat4 matrix1{1.0f};
        glm::vec4 frame0{0.0f};
        glm::vec4 frame1{0.0f};
        glm::vec4 frame2{0.0f};
    };

    struct PassInputs {
        RHITexture* color = nullptr;
        RHITexture* depth = nullptr;
        RHITexture* history = nullptr;
        RHITexture* auxiliary = nullptr;
    };

    void ensureTargets(uint32_t width, uint32_t height, DXGI_FORMAT format);
    void ensureSamplers();
    void createRootSignature();
    void createPipelines(DXGI_FORMAT format);
    void destroyPipelines();
    void passFullscreen(ID3D12GraphicsCommandList* cmd, const PassInputs& inputs, RHITexture& dst,
                        Pipeline& pipeline, const PostProcessConstants& constants);
    void copyToHistory(ID3D12GraphicsCommandList* cmd, RHITexture& src);
    void warnOnce(bool& flag, const char* effectName);

    DX12Context* m_ctx = nullptr;
    DX12DescriptorHeap* m_srvHeap = nullptr;
    DX12DescriptorHeap* m_rtvHeap = nullptr;
    DX12DescriptorHeap* m_samplerHeap = nullptr;

    PostProcessSettings m_settings{};
    bool m_enabled = true;
    PostProcessFrameData m_frameData{};

    RHITexture m_ping;
    RHITexture m_pong;
    RHITexture m_history;
    RHITexture m_velocity;
    RHITexture m_ssaoFactor;
    RHITexture m_bloomExtract;
    RHITexture m_bloomBlur;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT m_pipelineFormat = DXGI_FORMAT_UNKNOWN;
    bool m_historyValid = false;
    uint32_t m_featureMask = 0;
    bool m_wasEnabled = true;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Pipeline m_copyPipeline { nullptr, "Copy" };
    Pipeline m_presentPipeline { nullptr, "Present" };
    Pipeline m_velocityPipeline { nullptr, "Velocity" };
    Pipeline m_taaPipeline  { nullptr, "TAA" };
    Pipeline m_motionBlurPipeline{ nullptr, "Motion Blur" };
    Pipeline m_volumetricPipeline{ nullptr, "Volumetric" };
    Pipeline m_bloomExtractPipeline{ nullptr, "Bloom Extract" };
    Pipeline m_bloomBlurPipeline{ nullptr, "Bloom Blur" };
    Pipeline m_bloomCombinePipeline{ nullptr, "Bloom Combine" };
    Pipeline m_tonemapPipeline { nullptr, "ToneMap" };
    Pipeline m_dofPipeline  { nullptr, "DOF" };
    Pipeline m_ssgiPipeline { nullptr, "SSGI" };
    Pipeline m_ssaoPipeline { nullptr, "SSAO" };
    Pipeline m_ssaoApplyPipeline{ nullptr, "SSAO Apply" };

    uint32_t m_srvTableIndex = UINT32_MAX;
    uint32_t m_srvTableCursor = 0;
    static constexpr uint32_t kSrvTablesPerFrame = 64;

    RHISampler m_linearClamp;
    RHISampler m_linearWrap;

    bool m_warnVelocity = false;
    bool m_warnTAA = false;
    bool m_warnMotionBlur = false;
    bool m_warnVolumetric = false;
    bool m_warnBloom = false;
    bool m_warnTonemap = false;
    bool m_warnDOF = false;
    bool m_warnSSGI = false;
    bool m_warnSSAO = false;

public:
    void setFrameData(const PostProcessFrameData& frameData) { m_frameData = frameData; }
    void invalidateHistory() { m_historyValid = false; }
};

} // namespace Demon
