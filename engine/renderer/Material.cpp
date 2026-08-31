// ==============================================================================
//  DemonEngine::Material  –  Implementation
// ==============================================================================
#include "Material.h"
#include "core/Logger.h"
#include <fstream>
#include <sstream>

namespace Demon {

std::shared_ptr<Material> Material::createDefault()
{
    auto mat = std::make_shared<Material>("Default");
    mat->setAlbedo({1, 1, 1, 1});
    mat->setMetallic(0.0f);
    mat->setRoughness(0.5f);
    mat->clearDirty();  // freshly created — not dirty yet
    return mat;
}

std::shared_ptr<Material> Material::createFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        DEMON_LOG_ERROR("Material::createFromFile: cannot open '{}'", path);
        return createDefault();
    }

    auto mat = std::make_shared<Material>(path);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string token; ss >> token;

        if (token == "Kd") {
            glm::vec3 c; ss >> c.r >> c.g >> c.b;
            mat->setAlbedo({c, 1.0f});
        } else if (token == "Ke") {
            glm::vec3 e; ss >> e.r >> e.g >> e.b;
            mat->setEmissive(e, glm::length(e));
        } else if (token == "map_Kd") {
            std::string p; ss >> p; mat->setAlbedoTexture(p);
        } else if (token == "map_bump" || token == "bump") {
            std::string p; ss >> p; mat->setNormalTexture(p);
        } else if (token == "map_Pm") {
            std::string p; ss >> p; mat->setMetallicTexture(p);
        } else if (token == "map_Ke") {
            std::string p; ss >> p; mat->setEmissiveTexture(p);
        } else if (token == "d") {
            float alpha; ss >> alpha;
            if (alpha < 1.0f) mat->setAlphaBlend(true);
        }
    }

    // Newly loaded from file — not dirty (nothing needs re-syncing to GPU yet)
    mat->clearDirty();
    DEMON_LOG_INFO("Material loaded: '{}'", path);
    return mat;
}

} // namespace Demon
