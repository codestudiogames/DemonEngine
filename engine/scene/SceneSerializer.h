#pragma once
// ==============================================================================
//  DemonEngine::SceneSerializer
//  Saves/loads scenes as human-readable JSON (no external lib — hand-rolled).
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {
class Scene;

class SceneSerializer {
public:
    explicit SceneSerializer(std::shared_ptr<Scene> scene);

    // Serialise to file (returns true on success)
    bool serialize  (const std::string& filepath);
    // Serialize to binary file (returns true on success)
    bool serializeBinary(const std::string& filepath);
    // Deserialise from file (returns true on success)
    bool deserialize(const std::string& filepath);
    // Deserialize from binary file (returns true on success)
    bool deserializeBinary(const std::string& filepath);

    // In-memory variants
    std::string serializeToString();
    bool        deserializeFromString(const std::string& json);

private:
    std::shared_ptr<Scene> m_scene;
};

} // namespace Demon
