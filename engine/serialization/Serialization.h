
#pragma once
// ==============================================================================
//  DemonEngine::Serialization
//  Lightweight JSON + Binary serialization utilities (no external dependencies).
// ==============================================================================
#include "core/DemonPCH.h"

namespace Demon {

// ------------------------------------------------------------------------------
// Binary Writer / Reader
// ------------------------------------------------------------------------------
class BinaryWriter {
public:
    BinaryWriter() = default;
    ~BinaryWriter();

    BinaryWriter(const BinaryWriter&)            = delete;
    BinaryWriter& operator=(const BinaryWriter&) = delete;

    // Main-thread only: file IO.
    bool open(const std::filesystem::path& path);
    void close();
    [[nodiscard]] bool isOpen() const;

    void writeBytes(const void* data, size_t size);
    void writeString(std::string_view str);

    template<typename T>
    void write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryWriter::write requires trivially copyable type");
        writeBytes(&value, sizeof(T));
    }

private:
    std::ofstream m_stream;
};

class BinaryReader {
public:
    BinaryReader() = default;
    ~BinaryReader();

    BinaryReader(const BinaryReader&)            = delete;
    BinaryReader& operator=(const BinaryReader&) = delete;

    // Main-thread only: file IO.
    bool open(const std::filesystem::path& path);
    void close();
    [[nodiscard]] bool isOpen() const;

    bool readBytes(void* data, size_t size);
    bool readString(std::string& out);

    template<typename T>
    bool read(T& out) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "BinaryReader::read requires trivially copyable type");
        return readBytes(&out, sizeof(T));
    }

private:
    std::ifstream m_stream;
};

// ------------------------------------------------------------------------------
// JSON Value + Document
// ------------------------------------------------------------------------------
class JsonValue {
public:
    using Array  = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;

    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue();
    JsonValue(std::nullptr_t);
    JsonValue(bool value);
    JsonValue(double value);
    JsonValue(int64_t value);
    JsonValue(uint64_t value);
    JsonValue(const char* value);
    JsonValue(std::string value);
    JsonValue(Array value);
    JsonValue(Object value);

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool isNull()   const { return m_type == Type::Null; }
    [[nodiscard]] bool isBool()   const { return m_type == Type::Bool; }
    [[nodiscard]] bool isNumber() const { return m_type == Type::Number; }
    [[nodiscard]] bool isString() const { return m_type == Type::String; }
    [[nodiscard]] bool isArray()  const { return m_type == Type::Array; }
    [[nodiscard]] bool isObject() const { return m_type == Type::Object; }

    [[nodiscard]] bool               asBool(bool fallback = false) const;
    [[nodiscard]] double             asNumber(double fallback = 0.0) const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array&       asArray() const;
    [[nodiscard]] const Object&      asObject() const;

    [[nodiscard]] Array&  asArray();
    [[nodiscard]] Object& asObject();

    [[nodiscard]] const JsonValue* find(std::string_view key) const;
    [[nodiscard]] JsonValue*       find(std::string_view key);

private:
    Type m_type = Type::Null;
    std::variant<std::monostate, bool, double, std::string, Array, Object> m_value;
};

class JsonDocument {
public:
    JsonDocument() = default;

    // Thread-safe (stateless per instance). No global/static state.
    bool parse(std::string_view text);

    [[nodiscard]] const JsonValue& root() const { return m_root; }
    [[nodiscard]] const std::string& error() const { return m_error; }
    [[nodiscard]] size_t errorOffset() const { return m_errorOffset; }

private:
    JsonValue   m_root;
    std::string m_error;
    size_t      m_errorOffset = 0;
};

// ------------------------------------------------------------------------------
// JSON Writer
// ------------------------------------------------------------------------------
class JsonWriter {
public:
    explicit JsonWriter(bool pretty = true, int indentSize = 2);

    void reset();

    void beginObject();
    void endObject();
    void beginArray();
    void endArray();
    void key(std::string_view name);

    void value(bool v);
    void value(int64_t v);
    void value(uint64_t v);
    void value(double v);
    void value(float v);
    void value(std::string_view v);
    void value(const std::string& v);
    void nullValue();

    [[nodiscard]] const std::string& str() const { return m_out; }

private:
    enum class ScopeType { Object, Array };
    struct Scope {
        ScopeType type = ScopeType::Object;
        bool first = true;
    };

    void beginValue();
    void writeIndent();
    void writeEscaped(std::string_view s);

    std::vector<Scope> m_stack;
    std::string m_out;
    bool m_pretty = true;
    int m_indentSize = 2;
    bool m_expectValue = false;
};

} // namespace Demon
