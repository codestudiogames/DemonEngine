#pragma once
// ==============================================================================
//  DemonEngine::Material
//  PBR material with albedo, normal, metallic-roughness, AO, emissive textures.
//
//  Optimisations vs original:
//   - All setters track a m_dirty flag so Scene::onRender() can avoid
//     invalidating the GPU descriptor on frames where nothing changed.
//   - Texture path setters only mark dirty when the path actually changes,
//     preventing redundant GPU re-uploads every frame.
//   - Removed duplicate #include guard block that was appended at the bottom.
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

class Texture;

struct MaterialData {
    glm::vec4 albedoColor      = {1, 1, 1, 1};
    float     metallic         = 0.0f;
    float     roughness        = 0.5f;
    float     ao               = 1.0f;
    float     emissiveStrength = 0.0f;
    glm::vec3 emissiveColor    = {0, 0, 0};
    bool      doubleSided      = false;
    bool      alphaBlend       = false;
    float     alphaCutoff      = 0.5f;
};

class Material {
public:
    Material() = default;
    explicit Material(std::string name) : m_name(std::move(name)) {}

    // ── Setters — mark dirty so the renderer re-uploads only when needed ──────
    void setAlbedo(const glm::vec4& color) {
        if (m_data.albedoColor != color) { m_data.albedoColor = color; markDirty(); }
    }
    void setMetallic(float v) {
        if (m_data.metallic != v) { m_data.metallic = v; markDirty(); }
    }
    void setRoughness(float v) {
        if (m_data.roughness != v) { m_data.roughness = v; markDirty(); }
    }
    void setEmissive(const glm::vec3& c, float s) {
        if (m_data.emissiveColor != c || m_data.emissiveStrength != s) {
            m_data.emissiveColor = c; m_data.emissiveStrength = s; markDirty();
        }
    }
    void setDoubleSided(bool v) {
        if (m_data.doubleSided != v) { m_data.doubleSided = v; markDirty(); }
    }
    void setAlphaBlend(bool v) {
        if (m_data.alphaBlend != v) { m_data.alphaBlend = v; markDirty(); }
    }
    void setAlphaCutoff(float v) {
        if (m_data.alphaCutoff != v) { m_data.alphaCutoff = v; markDirty(); }
    }

    // Texture path setters — only invalidate GPU state when the path changes
    void setAlbedoTexture(std::string p) {
        if (m_albedoPath != p) { m_albedoPath = std::move(p); uploaded = false; }
    }
    void setNormalTexture(std::string p) {
        if (m_normalPath != p) { m_normalPath = std::move(p); uploaded = false; }
    }
    void setMetallicTexture(std::string p) {
        if (m_metallicPath != p) { m_metallicPath = std::move(p); uploaded = false; }
    }
    void setEmissiveTexture(std::string p) {
        if (m_emissivePath != p) { m_emissivePath = std::move(p); uploaded = false; }
    }

    // ── Getters ───────────────────────────────────────────────────────────────
    [[nodiscard]] const MaterialData& getData()         const { return m_data; }
    [[nodiscard]] const std::string&  getName()         const { return m_name; }
    [[nodiscard]] const std::string&  getAlbedoPath()   const { return m_albedoPath; }
    [[nodiscard]] const std::string&  getNormalPath()   const { return m_normalPath; }
    [[nodiscard]] const std::string&  getMetallicPath() const { return m_metallicPath; }
    [[nodiscard]] const std::string&  getEmissivePath() const { return m_emissivePath; }

    // Consume the dirty flag — called by Scene::onRender before submitting
    [[nodiscard]] bool isDirty() const { return m_dirty; }
    void clearDirty()                  { m_dirty = false; }

    // ── GPU handles (filled by renderer) ─────────────────────────────────────
    uint32_t                  srvTableIndex    = UINT32_MAX; // base index into SRV heap
    bool                      uploaded         = false;
    std::shared_ptr<Texture>  albedoTexture;
    std::shared_ptr<Texture>  normalTexture;
    std::shared_ptr<Texture>  metallicTexture;
    std::shared_ptr<Texture>  emissiveTexture;

    // ── Factory ───────────────────────────────────────────────────────────────
    static std::shared_ptr<Material> createDefault();
    static std::shared_ptr<Material> createFromFile(const std::string& path);  // MTL

private:
    void markDirty() { m_dirty = true; }

    std::string  m_name;
    MaterialData m_data;
    bool         m_dirty        = false;
    std::string  m_albedoPath, m_normalPath, m_metallicPath, m_emissivePath;
};

} // namespace Demon
