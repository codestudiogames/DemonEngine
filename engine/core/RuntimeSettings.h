#pragma once
// ==============================================================================
//  DemonEngine::RuntimeSettings
//  Hot-reloadable settings parsed from engine.config.json
// ==============================================================================
#include "DemonPCH.h"

namespace Demon {

class RuntimeSettings {
public:
    struct Value {
        using Variant = std::variant<int64_t, double, bool, std::string>;
        Variant data;
    };

    bool load(const std::filesystem::path& path);
    bool update();

    void setHotReload(bool enabled) { m_hotReload = enabled; }
    [[nodiscard]] bool isHotReloadEnabled() const { return m_hotReload; }

    void setPollIntervalMs(uint32_t ms) { m_pollIntervalMs = ms; }

    [[nodiscard]] bool hasKey(std::string_view key) const;

    bool tryGetBool(std::string_view key, bool& out) const;
    bool tryGetInt(std::string_view key, int64_t& out) const;
    bool tryGetUInt64(std::string_view key, uint64_t& out) const;
    bool tryGetFloat(std::string_view key, double& out) const;
    bool tryGetString(std::string_view key, std::string& out) const;

private:
    using Map = std::unordered_map<std::string, Value>;

    bool parseJson(const std::string& text, Map& out, std::string& error);
    static uint64_t nowMs();

    std::filesystem::path         m_path;
    Map                           m_values;
    bool                          m_hotReload = true;
    uint32_t                      m_pollIntervalMs = 250;
    std::filesystem::file_time_type m_lastWriteTime{};
    uint64_t                      m_lastPollMs = 0;
};

} // namespace Demon
