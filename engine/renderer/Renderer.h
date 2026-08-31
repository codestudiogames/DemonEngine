#pragma once
// ==============================================================================
//  DemonEngine::Renderer
//  Owns the full DirectX 12 pipeline: context, swapchain, render targets,
//  command buffers, and the GUI-friendly DX12 backend hooks.
// ==============================================================================
#include "core/DemonPCH.h"
#include "core/Window.h"
#include "scene/Components.h"
#include "DX12Context.h"
#include "DX12Swapchain.h"
#include "DX12DescriptorHeap.h"
#include "RHITexture.h"
#include "PostProcessing.h"

namespace Demon {

    class Camera;
    class Mesh;
    class Material;
    class Texture;
    struct RenderStats {
        uint32_t drawCalls   = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount  = 0;
        uint32_t culledCount = 0;
        uint32_t occludedCount = 0;
        uint32_t gbufferDrawCalls = 0;
        uint32_t computeDispatches = 0;
        uint32_t computeTiles = 0;
        float    gpuTimeMs   = 0.0f;
    };

    struct ShadowSettings {
        bool  enabled = true;
        float maxDistance = 140.0f;
        float strength = 0.9f;
        float bias = 0.045f;      // world units along the light direction
        float normalBias = 1.6f;  // shadow-map texels along the surface normal
        float softness = 2.25f;
    };

    struct AtmosphereSettings {
        bool  enabled = true;
        float density = 1.0f;
        float heightFalloff = 0.15f;
        float anisotropy = 0.45f;
        float sunDiskSize = 0.035f;
        float sunDiskIntensity = 18.0f;
        float skyBlend = 0.65f;
        float aerialPerspective = 0.85f;
    };

    struct IBLSettings {
        float diffuseIntensity = 1.0f;
        float specularIntensity = 1.25f;
        float reflectionBoost = 1.15f;
        float localProbeBlend = 0.55f;
    };

    struct ReflectionProbeSettings {
        bool enabled = false;
        std::string assetPath;
        std::string texturePath;
        glm::vec3 center{0.0f, 2.0f, 0.0f};
        float intensity = 1.0f;
        glm::vec3 extents{12.0f, 6.0f, 12.0f};
        float blend = 0.65f;
        int priority = 0;
    };

    struct GlobalIlluminationSettings {
        enum class Mode : uint32_t {
            Realtime = 0,
            Baked = 1,
        };

        bool enabled = false;
        Mode mode = Mode::Realtime;
        float diffuseBounce = 0.38f;
        float specularBounce = 0.16f;
        float radius = 18.0f;
        float colorBleed = 0.22f;
        glm::vec3 tint{1.0f, 0.96f, 0.88f};
    };

    struct RendererSettings {
        ShadowSettings shadows{};
        AtmosphereSettings atmosphere{};
        IBLSettings ibl{};
        ReflectionProbeSettings reflectionProbe{};
        GlobalIlluminationSettings gi{};
    };

    struct WaterRenderParams {
        glm::vec4 deepColorAndTransparency{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 waveParams{0.0f};
        glm::vec4 surfaceParams{0.0f};
        glm::vec4 styleParams{0.0f};
    };

    class Renderer {
    public:
        explicit Renderer(Window& window);
        ~Renderer();

        void init();
        void shutdown();

        bool beginFrame();
        void beginScene(const Camera& camera);
        void setSceneTime(float timeSeconds);
        void setSceneLighting(const glm::vec3& direction,
                              const glm::vec3& color,
                              const glm::vec3& ambient,
                              bool directionalCastsShadows = true);
        struct LocalLight {
            glm::vec4 positionRange{0.0f};
            glm::vec4 directionType{0.0f};
            glm::vec4 colorIntensity{0.0f};
            glm::vec4 spotParams{0.0f};
        };
        void setLocalLights(const std::vector<LocalLight>& lights);
        struct DDGIProbe {
            glm::vec4 positionRadius{0.0f};
            glm::vec4 irradiance{0.0f};
            glm::vec4 visibility{0.0f};
        };
        void setDDGIProbes(const std::vector<DDGIProbe>& probes,
                           float intensity,
                           float normalBias,
                           float leakReduction,
                           bool enabled);
        void setSkybox(const std::string& texturePath, float intensity, bool enabled);
        void setFog(const struct FogComponent& fog);
        void setVolumetricFog(const struct VolumetricFogComponent* fog);
        void setLocalVolumetricFog(const struct LocalVolumetricFogComponent* fog,
                                   const glm::vec3& worldCenter = glm::vec3(0.0f));
        void setVolumetricClouds(const struct VolumetricCloudComponent* clouds);
        void setLensFlare(const struct LensFlareComponent* lensFlare);
        void setReflectionProbe(const std::string& assetPath, const glm::vec3& center, bool enabled, int priority = 0);
        void submit(const Mesh& mesh,
                    const Material& mat,
                    const glm::mat4& transform,
                    const std::vector<glm::mat4>* skinMatrices = nullptr,
                    bool highlight = false,
                    uint64_t entityId = 0,
                    int32_t subMeshIndex = -1,
                    bool castShadows = true,
                    bool editorOverlay = false);
        void submitWater(const Mesh& mesh,
                         const Material& mat,
                         const glm::mat4& transform,
                         const WaterRenderParams& params,
                         uint64_t entityId = 0);
        void endScene();
        void endFrame();

        void onResize(uint32_t width, uint32_t height);
        void resizeViewport(uint32_t width, uint32_t height);

        // Hot-reload: flush the GPU, then rebuild every shader-derived pipeline
        // state object (scene, sky, g-buffer, shadow, post-processing) from the
        // freshly compiled .cso files on disk. Safe to call between frames.
        // Returns false if any required graphics PSO failed to rebuild.
        bool reloadPipelines();

        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE getViewportDescriptor() const { return m_viewportSrv; }
        [[nodiscard]] DX12Context&     getContext()            { return m_context; }
        [[nodiscard]] const RenderStats& getStats()        const { return m_stats; }
        [[nodiscard]] PostProcessing& getPostProcessing() { return m_postProcessing; }
        [[nodiscard]] const PostProcessing& getPostProcessing() const { return m_postProcessing; }
        [[nodiscard]] RendererSettings& settings() { return m_settings; }
        [[nodiscard]] const RendererSettings& settings() const { return m_settings; }

        [[nodiscard]] DX12DescriptorHeap& getSrvHeap() { return m_srvHeap; }
        [[nodiscard]] DX12DescriptorHeap& getSamplerHeap() { return m_samplerHeap; }
        [[nodiscard]] uint32_t    getFrameIndex()      const { return m_frameIndex; }
        [[nodiscard]] DXGI_FORMAT getSwapchainFormat() const { return m_swapchain.getBackBufferFormat(); }
        [[nodiscard]] glm::vec3 getCameraPosition() const { return glm::vec3(m_sceneUbo.cameraPos); }

    private:
        static constexpr uint32_t k_maxFramesInFlight = DX12Context::k_frameCount;
        static constexpr uint32_t k_shadowCascadeCount = 4;
        static constexpr uint32_t k_shadowAtlasSize = 2048;
        static constexpr uint32_t k_maxAnimatedDraws = 512;
        static constexpr uint32_t k_maxLocalLights = 16;
        static constexpr uint32_t k_maxDDGIProbes = 32;
        static constexpr DXGI_FORMAT k_sceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        static constexpr DXGI_FORMAT k_gbufferAlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        static constexpr DXGI_FORMAT k_gbufferMaterialFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        static constexpr DXGI_FORMAT k_gbufferNormalFormat = DXGI_FORMAT_R16G16_FLOAT;
        static constexpr DXGI_FORMAT k_gbufferEmissiveFormat = DXGI_FORMAT_R11G11B10_FLOAT;
        static constexpr DXGI_FORMAT k_gbufferVelocityFormat = DXGI_FORMAT_R16G16_FLOAT;
        static constexpr uint32_t k_computeTileSize = 8;
        enum class DrawPipeline : uint8_t {
            Scene,
            Water,
        };

        struct DrawCommand {
            const Mesh*     mesh      = nullptr;
            const Material* material  = nullptr;
            glm::mat4       transform {1.0f};
            glm::vec4       albedo    {1.0f};
            float           roughness = 0.5f;
            float           metallic  = 0.0f;
            float           highlight = 0.0f;
            float           ao        = 1.0f;
            glm::vec4       texFlags  {0.0f};
            glm::vec4       customData{0.0f};
            bool            alphaBlend = false;
            bool            castShadows = true;
            bool            editorOverlay = false;
            glm::mat4       previousTransform {1.0f};
            const std::vector<glm::mat4>* skinMatrices = nullptr;
            bool            skinned = false;
            uint64_t        entityId  = 0;
            int32_t         subMeshIndex = -1;
            DrawPipeline    pipeline = DrawPipeline::Scene;
        };

        struct SceneUBO {
            glm::mat4 viewProj  {1.0f};
            glm::mat4 previousViewProj {1.0f};
            glm::vec4 lightDir  {0.4f, -1.0f, 0.3f, 1.0f};
            glm::vec4 lightColor{1.0f,  1.0f, 1.0f, 1.0f};
            glm::vec4 ambient   {0.25f, 0.25f, 0.25f, 1.0f};
            glm::vec4 cameraPos {0.0f, 0.0f, 0.0f, 1.0f};
            glm::vec4 skyAmbient{0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 fogColorDensity {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 fogParams {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 shadowSplits {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 shadowParams0 {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 shadowParams1 {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 atmosphereParams0 {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 atmosphereParams1 {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 iblParams {1.0f, 1.25f, 1.15f, 0.55f};
            glm::vec4 reflectionProbeCenter {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 reflectionProbeExtents {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 giParams {0.0f, 0.38f, 0.16f, 0.22f};
            glm::vec4 giColor {1.0f, 0.96f, 0.88f, 18.0f};
            glm::vec4 localLightCount {0.0f, 0.0f, 0.0f, 0.0f};
            std::array<LocalLight, k_maxLocalLights> localLights{};
            glm::vec4 ddgiParams {0.0f, 0.0f, 0.65f, 0.35f};
            glm::vec4 ddgiParams1 {0.62f, 0.0f, 0.0f, 0.0f};
            std::array<DDGIProbe, k_maxDDGIProbes> ddgiProbes{};
            std::array<glm::mat4, k_shadowCascadeCount> lightViewProj{
                glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)
            };
            // One component per cascade. Shadow bias is authored in world units but
            // has to be compared in [0,1] depth space, and the cascades span wildly
            // different depth ranges (~5x between the near and far slice), so the
            // shader needs these to convert. Appended after lightViewProj so shaders
            // that only declare the shorter prefix of SceneCB stay layout compatible.
            glm::vec4 shadowDepthRange{1.0f, 1.0f, 1.0f, 1.0f}; // world units spanned by the cascade's depth range
            glm::vec4 shadowTexelWorld{1.0f, 1.0f, 1.0f, 1.0f}; // world units covered by one shadow texel
        };

        struct ShadowSceneUBO {
            glm::mat4 viewProj{1.0f};
        };

        void recreateSwapchain();
        void createScenePipeline();
        void createSkyPipeline();
        void createGBufferPipeline();
        void createComputeStack();
        void createShadowResources();
        void createShadowPipeline();
        void destroyScenePipeline();
        void destroySkyPipeline();
        void destroyGBufferPipeline();
        void destroyComputeStack();
        void destroyShadowResources();
        void createViewportResources(uint32_t width, uint32_t height);
        void destroyViewportResources();
        void createGBufferResources(uint32_t width, uint32_t height);
        void destroyGBufferResources();
        void createComputeResources(uint32_t width, uint32_t height);
        void destroyComputeResources();

        void flushPendingUploads();
        void doUploadMesh(const Mesh& mesh);
        void doUploadMaterial(const Material& mat);

        void updateShadowCascades();
        void renderShadowAtlas(ID3D12GraphicsCommandList* cmd);
        void renderGBuffer(ID3D12GraphicsCommandList* cmd);
        void runComputeStack(ID3D12GraphicsCommandList* cmd);
        void runPostLightingComputeStack(ID3D12GraphicsCommandList* cmd);
        void renderViewport(ID3D12GraphicsCommandList* cmd);
        void destroyUploadedMeshes();
        void destroyUploadedMaterials();

        void initSceneConstantBuffer();
        void initSkinningConstantBuffer();
        void initShadowConstantBuffer();
        uint32_t getSceneCbvIndex(uint32_t frameIndex) const;
        uint32_t getShadowCbvIndex(uint32_t frameIndex, uint32_t cascadeIndex) const;
        static glm::vec2 halton23(uint32_t index);

        // Frustum culling
        std::array<glm::vec4, 6> m_frustumPlanes{};
        void extractFrustumPlanes(const glm::mat4& viewProj);
        [[nodiscard]] bool isSphereInFrustum(const glm::vec3& center, float radius) const;

        Window& m_window;
        DX12Context m_context;
        DX12Swapchain m_swapchain;

        DX12DescriptorHeap m_rtvHeap;
        DX12DescriptorHeap m_dsvHeap;
        DX12DescriptorHeap m_srvHeap;
        DX12DescriptorHeap m_samplerHeap;
        uint32_t m_samplerWrapIndex = UINT32_MAX;
        uint32_t m_samplerClampIndex = UINT32_MAX;

        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_scenePso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_sceneTransparentPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_sceneOverlayPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_waterPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_skyPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_shadowPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_gbufferPso;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_computeRootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_gbufferTileClassifyPso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_gbufferSsaoPreparePso;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_hdrLuminanceReducePso;

        Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneCb;
        uint8_t* m_sceneCbMapped = nullptr;
        uint32_t m_sceneCbStride = 0;
        std::array<uint32_t, k_maxFramesInFlight> m_sceneCbvIndex{};
        Microsoft::WRL::ComPtr<ID3D12Resource> m_skinningCb;
        uint8_t* m_skinningCbMapped = nullptr;
        uint32_t m_skinningCbStride = 0;
        uint32_t m_skinningSlotCursor = 1;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowCb;
        uint8_t* m_shadowCbMapped = nullptr;
        uint32_t m_shadowCbStride = 0;
        std::array<uint32_t, k_maxFramesInFlight * k_shadowCascadeCount> m_shadowCbvIndex{};

        uint32_t m_frameIndex = 0;
        bool     m_frameStarted = false;
        bool     m_skipPresentOnResize = false;
        uint32_t m_pendingSwapchainWidth  = 0;
        uint32_t m_pendingSwapchainHeight = 0;
        bool     m_swapchainResizePending = false;
        RenderStats m_stats{};
        glm::mat4 m_viewProjection{1.0f};
        glm::mat4 m_unjitteredViewProjection{1.0f};
        glm::mat4 m_viewMatrix{1.0f};
        glm::mat4 m_projectionMatrix{1.0f};
        glm::mat4 m_previousViewProjection{1.0f};
        SceneUBO  m_sceneUbo{};
        std::array<glm::mat4, k_shadowCascadeCount> m_shadowMatrices{
            glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)
        };
        std::array<float, k_shadowCascadeCount> m_shadowSplitDistances{0.0f, 0.0f, 0.0f, 0.0f};
        // Shadow diagnostics. An empty cascade atlas renders identically to a fully
        // lit scene, so the resolved setup is logged once and on every meaningful
        // change rather than left silent.
        bool      m_shadowSetupLogged = false;
        bool      m_shadowDisabledLogged = false;
        glm::vec3 m_loggedShadowLightDir{0.0f, 0.0f, 0.0f};
        uint32_t  m_loggedShadowCasterCount = UINT32_MAX;
        glm::vec2 m_currentJitter{0.0f, 0.0f};
        glm::vec2 m_previousJitter{0.0f, 0.0f};
        glm::vec3 m_previousCameraPosition{0.0f, 0.0f, 0.0f};
        glm::vec3 m_previousCameraForward{0.0f, 0.0f, -1.0f};
        float m_cameraNearClip = 0.1f;
        float m_cameraFarClip = 1000.0f;
        bool m_hasPreviousFrame = false;
        bool m_temporalResetPending = true;
        bool m_previousPostProcessingEnabled = true;
        bool m_previousTaaEnabled = true;
        bool m_previousMotionBlurEnabled = true;
        float m_previousTaaJitterScale = 1.0f;

        std::vector<DrawCommand> m_drawList;
        std::vector<const Mesh*>     m_pendingMeshUploads;
        std::vector<const Material*> m_pendingMaterialUploads;
        std::unordered_set<const Mesh*>     m_uploadedMeshes;
        std::unordered_set<const Material*> m_uploadedMaterials;

        std::shared_ptr<Texture> m_whiteTexture;
        std::shared_ptr<Texture> m_blackTexture;
        std::shared_ptr<Texture> m_normalTexture;
        std::shared_ptr<Texture> m_reflectionProbeTexture;
        uint32_t m_defaultMaterialTableIndex = UINT32_MAX;
        uint32_t m_environmentSrvTableIndex = UINT32_MAX;

        // Skybox (equirectangular)
        std::shared_ptr<Texture> m_skyTexture;
        std::shared_ptr<Mesh>    m_skySphere;
        std::string              m_skyPath;
        float                    m_skyIntensity = 1.0f;
        bool                     m_skyEnabled   = false;
        glm::vec3                m_skyAverage   = {0.0f, 0.0f, 0.0f};
        uint32_t                 m_skySrvTableIndex = UINT32_MAX;

        // Fog
        FogComponent m_fog{};
        RendererSettings m_settings{};
        RHITexture m_shadowAtlas;

        // Offscreen viewport
        RHITexture m_viewportColor;
        RHITexture m_viewportDepth;
        RHITexture m_viewportVelocity;
        D3D12_GPU_DESCRIPTOR_HANDLE m_viewportSrv{};
        RHITexture* m_viewportOutput = nullptr;
        uint32_t m_viewportWidth = 0;
        uint32_t m_viewportHeight = 0;
        uint32_t m_pendingViewportWidth  = 0;
        uint32_t m_pendingViewportHeight = 0;
        bool     m_viewportResizePending = false;

        // Phase 1 deferred data. The forward path still shades the viewport,
        // while these targets feed temporal velocity and compute classification.
        RHITexture m_gbufferAlbedo;
        RHITexture m_gbufferMaterial;
        RHITexture m_gbufferNormal;
        RHITexture m_gbufferEmissive;
        RHITexture m_gbufferVelocity;
        RHITexture m_gbufferDepth;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_gbufferTileMask;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_ssaoTileMask;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_hdrLuminanceTiles;
        uint32_t m_gbufferTileMaskUavIndex = UINT32_MAX;
        uint32_t m_ssaoTileMaskUavIndex = UINT32_MAX;
        uint32_t m_hdrLuminanceTilesUavIndex = UINT32_MAX;
        uint32_t m_gbufferTileCountX = 0;
        uint32_t m_gbufferTileCountY = 0;

        // Post-processing
        PostProcessing m_postProcessing;
        std::unordered_map<uint64_t, glm::mat4> m_previousTransforms;
    };

} // namespace Demon
