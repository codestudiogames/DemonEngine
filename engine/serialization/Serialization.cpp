// ==============================================================================
//  DemonEngine::Serialization  -  Implementation
// ==============================================================================
#include "Serialization.h"
#include "core/Logger.h"
#include <cctype>

namespace Demon {

// ------------------------------------------------------------------------------
// Binary Writer / Reader
// ------------------------------------------------------------------------------
BinaryWriter::~BinaryWriter() { close(); }

bool BinaryWriter::open(const std::filesystem::path& path)
{
    close();
    m_stream.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!m_stream.is_open()) {
        DEMON_LOG_ERROR("BinaryWriter: cannot open '{}'", path.string());
        return false;
    }
    return true;
}

void BinaryWriter::close()
{
    if (m_stream.is_open())
        m_stream.close();
}

bool BinaryWriter::isOpen() const { return m_stream.is_open(); }

void BinaryWriter::writeBytes(const void* data, size_t size)
{
    if (!m_stream.is_open()) return;
    m_stream.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void BinaryWriter::writeString(std::string_view str)
{
    uint32_t len = static_cast<uint32_t>(str.size());
    write(len);
    if (len > 0)
        writeBytes(str.data(), len);
}

BinaryReader::~BinaryReader() { close(); }

bool BinaryReader::open(const std::filesystem::path& path)
{
    close();
    m_stream.open(path, std::ios::binary | std::ios::in);
    if (!m_stream.is_open()) {
        DEMON_LOG_ERROR("BinaryReader: cannot open '{}'", path.string());
        return false;
    }
    return true;
}

void BinaryReader::close()
{
    if (m_stream.is_open())
        m_stream.close();
}

bool BinaryReader::isOpen() const { return m_stream.is_open(); }

bool BinaryReader::readBytes(void* data, size_t size)
{
    if (!m_stream.is_open()) return false;
    m_stream.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return m_stream.good();
}

bool BinaryReader::readString(std::string& out)
{
    uint32_t len = 0;
    if (!read(len)) return false;
    out.resize(len);
    if (len == 0) return true;
    return readBytes(out.data(), len);
}

// ------------------------------------------------------------------------------
// JSON Value
// ------------------------------------------------------------------------------
JsonValue::JsonValue() : m_type(Type::Null), m_value(std::monostate{}) {}
JsonValue::JsonValue(std::nullptr_t) : JsonValue() {}
JsonValue::JsonValue(bool value) : m_type(Type::Bool), m_value(value) {}
JsonValue::JsonValue(double value) : m_type(Type::Number), m_value(value) {}
JsonValue::JsonValue(int64_t value) : m_type(Type::Number), m_value(static_cast<double>(value)) {}
JsonValue::JsonValue(uint64_t value) : m_type(Type::Number), m_value(static_cast<double>(value)) {}
JsonValue::JsonValue(const char* value) : m_type(Type::String), m_value(std::string(value)) {}
JsonValue::JsonValue(std::string value) : m_type(Type::String), m_value(std::move(value)) {}
JsonValue::JsonValue(Array value) : m_type(Type::Array), m_value(std::move(value)) {}
JsonValue::JsonValue(Object value) : m_type(Type::Object), m_value(std::move(value)) {}

bool JsonValue::asBool(bool fallback) const
{
    if (!isBool()) return fallback;
    return std::get<bool>(m_value);
}

double JsonValue::asNumber(double fallback) const
{
    if (!isNumber()) return fallback;
    return std::get<double>(m_value);
}

const std::string& JsonValue::asString() const
{
    static const std::string empty;
    if (!isString()) return empty;
    return std::get<std::string>(m_value);
}

const JsonValue::Array& JsonValue::asArray() const
{
    static const Array empty;
    if (!isArray()) return empty;
    return std::get<Array>(m_value);
}

const JsonValue::Object& JsonValue::asObject() const
{
    static const Object empty;
    if (!isObject()) return empty;
    return std::get<Object>(m_value);
}

JsonValue::Array& JsonValue::asArray()
{
    if (!isArray()) {
        m_type = Type::Array;
        m_value = Array{};
    }
    return std::get<Array>(m_value);
}

JsonValue::Object& JsonValue::asObject()
{
    if (!isObject()) {
        m_type = Type::Object;
        m_value = Object{};
    }
    return std::get<Object>(m_value);
}

const JsonValue* JsonValue::find(std::string_view key) const
{
    if (!isObject()) return nullptr;
    const auto& obj = std::get<Object>(m_value);
    auto it = obj.find(std::string(key));
    if (it == obj.end()) return nullptr;
    return &it->second;
}

JsonValue* JsonValue::find(std::string_view key)
{
    if (!isObject()) return nullptr;
    auto& obj = std::get<Object>(m_value);
    auto it = obj.find(std::string(key));
    if (it == obj.end()) return nullptr;
    return &it->second;
}

// ------------------------------------------------------------------------------
// JSON Parser (internal)
// ------------------------------------------------------------------------------
namespace {

struct JsonParser {
    std::string_view text;
    size_t pos = 0;
    std::string error;
    size_t errorOffset = 0;

    bool parse(JsonValue& out) {
        skipWhitespace();
        if (!parseValue(out)) return false;
        skipWhitespace();
        if (pos != text.size()) {
            setError("Trailing characters after JSON");
            return false;
        }
        return true;
    }

    void setError(const char* msg) {
        if (!error.empty()) return;
        error = msg;
        errorOffset = pos;
    }

    void skipWhitespace() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            ++pos;
        }
    }

    bool consume(char expected) {
        if (pos >= text.size() || text[pos] != expected)
            return false;
        ++pos;
        return true;
    }

    bool parseValue(JsonValue& out) {
        if (pos >= text.size()) {
            setError("Unexpected end of input");
            return false;
        }
        char c = text[pos];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string s;
            if (!parseString(s)) return false;
            out = JsonValue(std::move(s));
            return true;
        }
        if (c == 't' || c == 'f' || c == 'n')
            return parseLiteral(out);
        return parseNumber(out);
    }

    bool parseObject(JsonValue& out) {
        if (!consume('{')) return false;
        JsonValue::Object obj;
        skipWhitespace();
        if (consume('}')) {
            out = JsonValue(std::move(obj));
            return true;
        }
        while (pos < text.size()) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) {
                setError("Expected ':' in object");
                return false;
            }
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value)) return false;
            obj[std::move(key)] = std::move(value);
            skipWhitespace();
            if (consume('}')) break;
            if (!consume(',')) {
                setError("Expected ',' or '}' in object");
                return false;
            }
        }
        out = JsonValue(std::move(obj));
        return true;
    }

    bool parseArray(JsonValue& out) {
        if (!consume('[')) return false;
        JsonValue::Array arr;
        skipWhitespace();
        if (consume(']')) {
            out = JsonValue(std::move(arr));
            return true;
        }
        while (pos < text.size()) {
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value)) return false;
            arr.emplace_back(std::move(value));
            skipWhitespace();
            if (consume(']')) break;
            if (!consume(',')) {
                setError("Expected ',' or ']' in array");
                return false;
            }
        }
        out = JsonValue(std::move(arr));
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            setError("Expected string");
            return false;
        }
        out.clear();
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= text.size()) {
                    setError("Invalid escape sequence");
                    return false;
                }
                char esc = text[pos++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 > text.size()) {
                            setError("Invalid unicode escape");
                            return false;
                        }
                        uint32_t code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = text[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= (h - '0');
                            else if (h >= 'a' && h <= 'f') code |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= (h - 'A' + 10);
                            else {
                                setError("Invalid unicode escape");
                                return false;
                            }
                        }
                        if (code <= 0x7F) out.push_back(static_cast<char>(code));
                        else out.push_back('?'); // keep ASCII-only output
                        break;
                    }
                    default:
                        setError("Unknown escape sequence");
                        return false;
                }
            } else {
                out.push_back(c);
            }
        }
        setError("Unterminated string");
        return false;
    }

    bool parseLiteral(JsonValue& out) {
        if (text.compare(pos, 4, "true") == 0) {
            pos += 4;
            out = JsonValue(true);
            return true;
        }
        if (text.compare(pos, 5, "false") == 0) {
            pos += 5;
            out = JsonValue(false);
            return true;
        }
        if (text.compare(pos, 4, "null") == 0) {
            pos += 4;
            out = JsonValue();
            return true;
        }
        setError("Invalid literal");
        return false;
    }

    bool parseNumber(JsonValue& out) {
        size_t start = pos;
        if (text[pos] == '-' || text[pos] == '+')
            ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
                ++pos;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '-' || text[pos] == '+'))
                ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
                ++pos;
        }
        if (start == pos) {
            setError("Invalid number");
            return false;
        }

        std::string num(text.substr(start, pos - start));
        char* endPtr = nullptr;
        double value = std::strtod(num.c_str(), &endPtr);
        if (endPtr == num.c_str()) {
            setError("Invalid number");
            return false;
        }
        out = JsonValue(value);
        return true;
    }
};

} // namespace

bool JsonDocument::parse(std::string_view text)
{
    JsonParser parser;
    parser.text = text;
    parser.pos = 0;
    parser.error.clear();
    parser.errorOffset = 0;
    JsonValue root;
    if (!parser.parse(root)) {
        m_error = parser.error;
        m_errorOffset = parser.errorOffset;
        return false;
    }
    m_root = std::move(root);
    m_error.clear();
    m_errorOffset = 0;
    return true;
}

// ------------------------------------------------------------------------------
// JSON Writer
// ------------------------------------------------------------------------------
JsonWriter::JsonWriter(bool pretty, int indentSize)
    : m_pretty(pretty), m_indentSize(indentSize)
{
    reset();
}

void JsonWriter::reset()
{
    m_stack.clear();
    m_out.clear();
    m_expectValue = false;
}

void JsonWriter::beginValue()
{
    if (m_stack.empty()) return;
    auto& scope = m_stack.back();

    if (scope.type == ScopeType::Array) {
        if (!scope.first) m_out += ",";
        if (m_pretty) writeIndent();
        scope.first = false;
        return;
    }

    if (scope.type == ScopeType::Object) {
        if (m_expectValue) {
            m_expectValue = false;
            return;
        }
    }
}

void JsonWriter::writeIndent()
{
    m_out += "\n";
    int indent = static_cast<int>(m_stack.size()) * m_indentSize;
    for (int i = 0; i < indent; ++i) m_out += ' ';
}

void JsonWriter::writeEscaped(std::string_view s)
{
    m_out += '"';
    for (char c : s) {
        switch (c) {
            case '"': m_out += "\\\""; break;
            case '\\': m_out += "\\\\"; break;
            case '\b': m_out += "\\b"; break;
            case '\f': m_out += "\\f"; break;
            case '\n': m_out += "\\n"; break;
            case '\r': m_out += "\\r"; break;
            case '\t': m_out += "\\t"; break;
            default: m_out.push_back(c); break;
        }
    }
    m_out += '"';
}

void JsonWriter::beginObject()
{
    beginValue();
    m_out += "{";
    m_stack.push_back({ScopeType::Object, true});
}

void JsonWriter::endObject()
{
    if (m_stack.empty()) return;
    auto scope = m_stack.back();
    m_stack.pop_back();
    if (m_pretty && !scope.first) {
        writeIndent();
    }
    m_out += "}";
}

void JsonWriter::beginArray()
{
    beginValue();
    m_out += "[";
    m_stack.push_back({ScopeType::Array, true});
}

void JsonWriter::endArray()
{
    if (m_stack.empty()) return;
    auto scope = m_stack.back();
    m_stack.pop_back();
    if (m_pretty && !scope.first) {
        writeIndent();
    }
    m_out += "]";
}

void JsonWriter::key(std::string_view name)
{
    if (m_stack.empty()) return;
    auto& scope = m_stack.back();
    if (scope.type != ScopeType::Object) return;

    if (!scope.first) m_out += ",";
    if (m_pretty) writeIndent();
    writeEscaped(name);
    m_out += m_pretty ? ": " : ":";
    scope.first = false;
    m_expectValue = true;
}

void JsonWriter::value(bool v)
{
    beginValue();
    m_out += v ? "true" : "false";
}

void JsonWriter::value(int64_t v)
{
    beginValue();
    m_out += std::to_string(v);
}

void JsonWriter::value(uint64_t v)
{
    beginValue();
    m_out += std::to_string(v);
}

void JsonWriter::value(double v)
{
    beginValue();
    m_out += std::format("{:.6f}", v);
}

void JsonWriter::value(float v)
{
    beginValue();
    m_out += std::format("{:.6f}", static_cast<double>(v));
}

void JsonWriter::value(std::string_view v)
{
    beginValue();
    writeEscaped(v);
}

void JsonWriter::value(const std::string& v)
{
    value(std::string_view(v));
}

void JsonWriter::nullValue()
{
    beginValue();
    m_out += "null";
}

} // namespace Demon
