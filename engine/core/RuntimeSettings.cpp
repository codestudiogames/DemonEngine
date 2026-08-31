// ==============================================================================
//  DemonEngine::RuntimeSettings
// ==============================================================================
#include "RuntimeSettings.h"
#include "Logger.h"
#include <cctype>

namespace Demon {

namespace {

using Map = std::unordered_map<std::string, RuntimeSettings::Value>;

struct JsonReader {
    const char* cur = nullptr;
    const char* end = nullptr;

    void skipWs() {
        while (cur < end && std::isspace(static_cast<unsigned char>(*cur)))
            ++cur;
    }

    bool match(char c) {
        skipWs();
        if (cur < end && *cur == c) {
            ++cur;
            return true;
        }
        return false;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (cur >= end || *cur != '"')
            return false;
        ++cur;
        out.clear();
        while (cur < end) {
            char c = *cur++;
            if (c == '"')
                return true;
            if (c == '\\' && cur < end) {
                char esc = *cur++;
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    default:   out.push_back(esc);  break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parseLiteral(const char* literal) {
        skipWs();
        const char* start = cur;
        while (*literal && cur < end && *cur == *literal) {
            ++cur;
            ++literal;
        }
        if (*literal == '\0')
            return true;
        cur = start;
        return false;
    }

    bool parseNumber(RuntimeSettings::Value& out) {
        skipWs();
        const char* start = cur;
        bool hasDot = false;
        if (cur < end && (*cur == '-' || *cur == '+'))
            ++cur;
        while (cur < end) {
            char c = *cur;
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ++cur;
                continue;
            }
            if (c == '.' || c == 'e' || c == 'E') {
                hasDot = true;
                ++cur;
                continue;
            }
            break;
        }
        if (cur == start)
            return false;

        std::string_view token(start, static_cast<size_t>(cur - start));
        try {
            if (hasDot) {
                double v = std::stod(std::string(token));
                out.data = v;
            } else {
                int64_t v = std::stoll(std::string(token));
                out.data = v;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    bool parseArrayAsString(RuntimeSettings::Value& out) {
        skipWs();
        if (cur >= end || *cur != '[')
            return false;
        const char* start = cur;
        int depth = 0;
        bool inString = false;
        while (cur < end) {
            char c = *cur++;
            if (c == '"' && (cur == start + 1 || *(cur - 2) != '\\'))
                inString = !inString;
            if (inString)
                continue;
            if (c == '[')
                ++depth;
            else if (c == ']') {
                --depth;
                if (depth == 0)
                    break;
            }
        }
        if (depth != 0)
            return false;
        out.data = std::string(start, static_cast<size_t>(cur - start));
        return true;
    }
};

bool parseObject(JsonReader& r,
                 const std::string& prefix,
                 Map& out,
                 std::string& error);

bool parseValue(JsonReader& r,
                const std::string& key,
                Map& out,
                std::string& error)
{
    r.skipWs();
    if (r.cur >= r.end) {
        error = "Unexpected end of input.";
        return false;
    }

    if (*r.cur == '{') {
        return parseObject(r, key + ".", out, error);
    }

    RuntimeSettings::Value val;
    if (*r.cur == '"') {
        std::string s;
        if (!r.parseString(s)) {
            error = "Failed to parse string value.";
            return false;
        }
        val.data = std::move(s);
        out[key] = std::move(val);
        return true;
    }

    if (*r.cur == '[') {
        if (!r.parseArrayAsString(val)) {
            error = "Failed to parse array value.";
            return false;
        }
        out[key] = std::move(val);
        return true;
    }

    if (r.parseLiteral("true")) {
        val.data = true;
        out[key] = std::move(val);
        return true;
    }
    if (r.parseLiteral("false")) {
        val.data = false;
        out[key] = std::move(val);
        return true;
    }
    if (r.parseLiteral("null")) {
        return true;
    }

    if (r.parseNumber(val)) {
        out[key] = std::move(val);
        return true;
    }

    error = "Unknown value token.";
    return false;
}

bool parseObject(JsonReader& r,
                 const std::string& prefix,
                 Map& out,
                 std::string& error)
{
    if (!r.match('{')) {
        error = "Expected '{'.";
        return false;
    }

    r.skipWs();
    if (r.match('}'))
        return true;

    while (r.cur < r.end) {
        std::string key;
        if (!r.parseString(key)) {
            error = "Expected object key.";
            return false;
        }
        if (!r.match(':')) {
            error = "Expected ':' after key.";
            return false;
        }

        if (!parseValue(r, prefix + key, out, error))
            return false;

        r.skipWs();
        if (r.match('}'))
            return true;
        if (!r.match(',')) {
            error = "Expected ',' or '}' after value.";
            return false;
        }
    }

    error = "Unexpected end of input in object.";
    return false;
}

} // namespace

bool RuntimeSettings::load(const std::filesystem::path& path)
{
    m_path = path;
    if (!std::filesystem::exists(m_path)) {
        DEMON_LOG_WARN("RuntimeSettings: config file not found: {}", m_path.string());
        return false;
    }

    std::ifstream file(m_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        DEMON_LOG_ERROR("RuntimeSettings: failed to open {}", m_path.string());
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Map parsed;
    std::string error;
    if (!parseJson(content, parsed, error)) {
        DEMON_LOG_ERROR("RuntimeSettings: parse error: {}", error);
        return false;
    }

    m_values = std::move(parsed);
    m_lastWriteTime = std::filesystem::last_write_time(m_path);
    m_lastPollMs = nowMs();

    DEMON_LOG_INFO("RuntimeSettings: loaded {}", m_path.string());
    return true;
}

bool RuntimeSettings::update()
{
    if (!m_hotReload || m_path.empty())
        return false;

    const uint64_t now = nowMs();
    if (m_pollIntervalMs > 0 && now - m_lastPollMs < m_pollIntervalMs)
        return false;

    m_lastPollMs = now;
    if (!std::filesystem::exists(m_path))
        return false;

    std::filesystem::file_time_type time{};
    try {
        time = std::filesystem::last_write_time(m_path);
    } catch (...) {
        return false;
    }

    if (time <= m_lastWriteTime)
        return false;

    std::ifstream file(m_path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Map parsed;
    std::string error;
    if (!parseJson(content, parsed, error)) {
        DEMON_LOG_ERROR("RuntimeSettings: parse error: {}", error);
        return false;
    }

    m_values = std::move(parsed);
    m_lastWriteTime = time;
    DEMON_LOG_INFO("RuntimeSettings: reloaded {}", m_path.string());
    return true;
}

bool RuntimeSettings::hasKey(std::string_view key) const
{
    return m_values.find(std::string(key)) != m_values.end();
}

bool RuntimeSettings::tryGetBool(std::string_view key, bool& out) const
{
    auto it = m_values.find(std::string(key));
    if (it == m_values.end())
        return false;

    if (auto v = std::get_if<bool>(&it->second.data)) {
        out = *v;
        return true;
    }
    if (auto v = std::get_if<int64_t>(&it->second.data)) {
        out = (*v != 0);
        return true;
    }
    return false;
}

bool RuntimeSettings::tryGetInt(std::string_view key, int64_t& out) const
{
    auto it = m_values.find(std::string(key));
    if (it == m_values.end())
        return false;

    if (auto v = std::get_if<int64_t>(&it->second.data)) {
        out = *v;
        return true;
    }
    if (auto v = std::get_if<double>(&it->second.data)) {
        out = static_cast<int64_t>(*v);
        return true;
    }
    return false;
}

bool RuntimeSettings::tryGetUInt64(std::string_view key, uint64_t& out) const
{
    auto it = m_values.find(std::string(key));
    if (it == m_values.end())
        return false;

    if (auto v = std::get_if<int64_t>(&it->second.data)) {
        out = static_cast<uint64_t>(*v);
        return true;
    }
    if (auto v = std::get_if<double>(&it->second.data)) {
        out = static_cast<uint64_t>(*v);
        return true;
    }
    if (auto v = std::get_if<std::string>(&it->second.data)) {
        try {
            size_t idx = 0;
            out = std::stoull(*v, &idx, 0);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool RuntimeSettings::tryGetFloat(std::string_view key, double& out) const
{
    auto it = m_values.find(std::string(key));
    if (it == m_values.end())
        return false;

    if (auto v = std::get_if<double>(&it->second.data)) {
        out = *v;
        return true;
    }
    if (auto v = std::get_if<int64_t>(&it->second.data)) {
        out = static_cast<double>(*v);
        return true;
    }
    return false;
}

bool RuntimeSettings::tryGetString(std::string_view key, std::string& out) const
{
    auto it = m_values.find(std::string(key));
    if (it == m_values.end())
        return false;

    if (auto v = std::get_if<std::string>(&it->second.data)) {
        out = *v;
        return true;
    }
    return false;
}

bool RuntimeSettings::parseJson(const std::string& text, Map& out, std::string& error)
{
    JsonReader r;
    r.cur = text.data();
    r.end = text.data() + text.size();

    if (!parseObject(r, "", out, error))
        return false;

    r.skipWs();
    if (r.cur != r.end) {
        error = "Unexpected trailing characters.";
        return false;
    }
    return true;
}

uint64_t RuntimeSettings::nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace Demon
