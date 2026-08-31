#include "ScriptEngineSupport.h"

#include <cctype>
#include <functional>
#include <limits>
#include <regex>

#include "core/Logger.h"
#include "scene/Scene.h"

namespace Demon::ScriptDetail {

namespace {

struct PendingStatement {
    std::string eventName;
    std::string text;
    size_t      line = 0;
    size_t      column = 0;
    bool        isIf = false;
    std::vector<PendingStatement> thenBlock;
    std::vector<PendingStatement> elseBlock;
};

struct ScriptExecutionContext {
    Scene&                          scene;
    ScriptComponent&                script;
    const ScriptBehaviorDefinition& behavior;
    EntityID                        entityId = NULL_ENTITY;
    float                           deltaTime = 0.0f;
    std::string_view                eventName;
    std::string_view                payload;
    // Local variables declared with 'var' — cleared per event invocation
    std::unordered_map<std::string, double>      localFloats;
    std::unordered_map<std::string, std::string> localStrings;
    std::unordered_map<std::string, bool>        localBools;
    std::unordered_map<std::string, glm::vec3>   localVecs;
};

ScriptFieldValue makeDefaultFieldValue(const std::string& name, ScriptFieldType type, bool hidden)
{
    ScriptFieldValue value;
    value.name = name;
    value.type = type;
    value.hidden = hidden;
    return value;
}

std::string trimCopy(std::string_view text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    return std::string(text.substr(start, end - start));
}

size_t firstNonWhitespaceColumn(std::string_view text)
{
    for (size_t i = 0; i < text.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(text[i])))
            return i + 1;
    }
    return 1;
}

void addDiagnostic(std::vector<ScriptDiagnostic>& diagnostics,
                   const std::filesystem::path& path,
                   size_t line,
                   size_t column,
                   std::string reason)
{
    diagnostics.push_back(ScriptDiagnostic{
        .path = path,
        .line = line,
        .column = column,
        .reason = std::move(reason),
    });
}

bool startsWithKeyword(std::string_view text, std::string_view keyword)
{
    if (!text.starts_with(keyword))
        return false;
    if (text.size() == keyword.size())
        return true;

    const char next = text[keyword.size()];
    return std::isspace(static_cast<unsigned char>(next)) || next == '(' || next == '{' || next == ':';
}

bool isNumericType(ScriptFieldType type)
{
    return type == ScriptFieldType::Int || type == ScriptFieldType::Float;
}

const char* fieldTypeName(ScriptFieldType type)
{
    switch (type) {
        case ScriptFieldType::Bool:   return "bool";
        case ScriptFieldType::Int:    return "int";
        case ScriptFieldType::Float:  return "float";
        case ScriptFieldType::String: return "str";
        case ScriptFieldType::Vec3:   return "vec3";
        case ScriptFieldType::Entity: return "entity";
        case ScriptFieldType::Entity3D: return "entity3d";
        case ScriptFieldType::EntityImage: return "entity_image";
        case ScriptFieldType::EntityUI: return "entity_ui";
        default:                      return "unknown";
    }
}

ScriptFieldType parseFieldTypeToken(std::string_view text)
{
    const std::string lower = trimCopy(text);
    if (lower == "bool") return ScriptFieldType::Bool;
    if (lower == "int") return ScriptFieldType::Int;
    if (lower == "float") return ScriptFieldType::Float;
    if (lower == "str" || lower == "string") return ScriptFieldType::String;
    if (lower == "vec3") return ScriptFieldType::Vec3;
    if (lower == "entity") return ScriptFieldType::Entity;
    if (lower == "entity3d" || lower == "entity_3d") return ScriptFieldType::Entity3D;
    if (lower == "entityimg" || lower == "entity_img" || lower == "entity_image") return ScriptFieldType::EntityImage;
    if (lower == "entityui" || lower == "entity_ui") return ScriptFieldType::EntityUI;
    return ScriptFieldType::None;
}

bool isEntityFieldType(ScriptFieldType type)
{
    return type == ScriptFieldType::Entity ||
           type == ScriptFieldType::Entity3D ||
           type == ScriptFieldType::EntityImage ||
           type == ScriptFieldType::EntityUI;
}

ScriptFieldType inputFieldTypeFromToken(std::string_view token)
{
    const std::string text = trimCopy(token);
    if (text.ends_with("3D")) return ScriptFieldType::Entity3D;
    if (text.ends_with("IMG")) return ScriptFieldType::EntityImage;
    if (text.ends_with("UI")) return ScriptFieldType::EntityUI;
    if (text.starts_with("InputField")) return ScriptFieldType::Entity;
    return ScriptFieldType::None;
}

const ScriptFieldDefinition* findFieldDefinition(const ScriptBehaviorDefinition& behavior, std::string_view name)
{
    for (const ScriptFieldDefinition& field : behavior.fields) {
        if (field.name == name)
            return &field;
    }
    return nullptr;
}

ScriptFieldValue* findFieldValue(ScriptComponent& script, std::string_view name)
{
    for (ScriptFieldValue& field : script.fieldValues) {
        if (field.name == name)
            return &field;
    }
    return nullptr;
}

const ScriptFieldValue* findFieldValue(const ScriptComponent& script, std::string_view name)
{
    for (const ScriptFieldValue& field : script.fieldValues) {
        if (field.name == name)
            return &field;
    }
    return nullptr;
}

std::string normalizeEventName(std::string_view eventName)
{
    if (eventName == "on_tick" || eventName == "on_update")
        return "on_update";
    return std::string(eventName);
}

bool markEventPresent(ScriptBehaviorDefinition& behavior, std::string_view eventName)
{
    if (eventName == "on_spawn") {
        behavior.hasOnSpawn = true;
        return true;
    }
    if (eventName == "on_update") {
        behavior.hasOnTick = true;
        return true;
    }
    if (eventName == "on_trigger") {
        behavior.hasOnTrigger = true;
        return true;
    }
    if (eventName == "on_signal") {
        behavior.hasOnSignal = true;
        return true;
    }
    return false;
}

std::string* selectEventBody(ScriptBehaviorDefinition& behavior, std::string_view eventName)
{
    if (eventName == "on_spawn")
        return &behavior.onSpawnBody;
    if (eventName == "on_update")
        return &behavior.onTickBody;
    if (eventName == "on_trigger")
        return &behavior.onTriggerBody;
    if (eventName == "on_signal")
        return &behavior.onSignalBody;
    return nullptr;
}

ScriptTargetKind parseTransformTarget(std::string_view text)
{
    if (text == "transform.translation.x" || text == "transform.position.x") return ScriptTargetKind::TransformTranslationX;
    if (text == "transform.translation.y" || text == "transform.position.y") return ScriptTargetKind::TransformTranslationY;
    if (text == "transform.translation.z" || text == "transform.position.z") return ScriptTargetKind::TransformTranslationZ;
    if (text == "transform.rotation.x")    return ScriptTargetKind::TransformRotationX;
    if (text == "transform.rotation.y")    return ScriptTargetKind::TransformRotationY;
    if (text == "transform.rotation.z")    return ScriptTargetKind::TransformRotationZ;
    if (text == "transform.scale.x")       return ScriptTargetKind::TransformScaleX;
    if (text == "transform.scale.y")       return ScriptTargetKind::TransformScaleY;
    if (text == "transform.scale.z")       return ScriptTargetKind::TransformScaleZ;
    return ScriptTargetKind::None;
}

bool isTransformVector(std::string_view text)
{
    return text == "transform.translation" ||
           text == "transform.position" ||
           text == "transform.rotation" ||
           text == "transform.scale";
}

std::optional<ScriptAssignOp> parseAssignOp(std::string_view op)
{
    if (op == "=")  return ScriptAssignOp::Set;
    if (op == "+=") return ScriptAssignOp::Add;
    if (op == "-=") return ScriptAssignOp::Subtract;
    if (op == "*=") return ScriptAssignOp::Multiply;
    if (op == "/=") return ScriptAssignOp::Divide;
    return std::nullopt;
}

bool parseStringLiteral(std::string_view text, std::string& out)
{
    const std::string trimmed = trimCopy(text);
    if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"')
        return false;
    out = trimmed.substr(1, trimmed.size() - 2);
    return true;
}

bool parseVec3Literal(std::string_view text, glm::vec3& out)
{
    const std::string trimmed = trimCopy(text);
    if (trimmed.size() < 5 || trimmed.front() != '{' || trimmed.back() != '}')
        return false;

    std::string inner = trimmed.substr(1, trimmed.size() - 2);
    std::replace(inner.begin(), inner.end(), ',', ' ');
    std::istringstream stream(inner);
    stream >> out.x >> out.y >> out.z;
    return !stream.fail();
}

bool isSimpleIdentifier(std::string_view text)
{
    const std::string trimmed = trimCopy(text);
    if (trimmed.empty())
        return false;
    if (!(std::isalpha(static_cast<unsigned char>(trimmed.front())) || trimmed.front() == '_'))
        return false;

    for (const char ch : trimmed) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.'))
            return false;
    }
    return true;
}

bool parseFunctionCallText(std::string_view text, std::string& outName, std::vector<std::string>& outArgs)
{
    const std::string trimmed = trimCopy(text);
    const size_t open = trimmed.find('(');
    if (open == std::string::npos || trimmed.back() != ')')
        return false;

    outName = trimCopy(std::string_view(trimmed.data(), open));
    if (!isSimpleIdentifier(outName) || outName.find('.') != std::string::npos)
        return false;

    const std::string_view inner(trimmed.data() + open + 1, trimmed.size() - open - 2);
    outArgs.clear();
    std::string current;
    int parenDepth = 0;
    bool inString = false;
    bool escaped = false;
    for (char ch : inner) {
        if (inString) {
            current.push_back(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            current.push_back(ch);
            continue;
        }
        if (ch == '(') {
            ++parenDepth;
            current.push_back(ch);
            continue;
        }
        if (ch == ')') {
            --parenDepth;
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && parenDepth == 0) {
            outArgs.push_back(trimCopy(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if (inString || parenDepth != 0)
        return false;
    if (!trimCopy(current).empty())
        outArgs.push_back(trimCopy(current));
    return true;
}

bool isSignalFunctionName(std::string_view name)
{
    return name == "emit" || name == "signal" || name == "emitTo" || name == "broadcast";
}

bool isUIActionFunctionName(std::string_view name)
{
    return name == "ui_show" ||
           name == "ui_hide" ||
           name == "ui_toggle" ||
           name == "ui_set_visible" ||
           name == "ui_set_text" ||
           name == "ui_set_color" ||
           name == "ui_set_size";
}

size_t uiActionArgCount(std::string_view name)
{
    if (name == "ui_show") return 1;
    if (name == "ui_hide") return 1;
    if (name == "ui_toggle") return 1;
    if (name == "ui_set_visible") return 2;
    if (name == "ui_set_text") return 2;
    if (name == "ui_set_color") return 5;
    if (name == "ui_set_size") return 3;
    return std::numeric_limits<size_t>::max();
}

size_t physicsActionArgCount(std::string_view name)
{
    if (name == "physics_clear_velocity") return 0;
    if (name == "physics_set_gravity") return 1;
    if (name == "physics_set_gravity_scale") return 1;
    if (name == "physics_set_mass") return 1;
    if (name == "physics_set_kinematic") return 1;
    if (name == "physics_set_trigger") return 1;
    if (name == "physics_set_friction") return 1;
    if (name == "physics_set_restitution") return 1;
    if (name == "physics_set_velocity") return 3;
    if (name == "physics_add_velocity") return 3;
    if (name == "physics_add_force") return 3;
    if (name == "physics_add_impulse") return 3;
    if (name == "physics_set_box") return 3;
    if (name == "physics_set_collider_offset") return 3;
    return std::numeric_limits<size_t>::max();
}

size_t physicsNumericArgCount(std::string_view name)
{
    if (name == "physics_velocity_x") return 0;
    if (name == "physics_velocity_y") return 0;
    if (name == "physics_velocity_z") return 0;
    if (name == "physics_speed") return 0;
    if (name == "physics_grounded") return 0;
    return std::numeric_limits<size_t>::max();
}

bool isPhysicsActionFunctionName(std::string_view name)
{
    return physicsActionArgCount(name) != std::numeric_limits<size_t>::max();
}

bool isPhysicsNumericFunctionName(std::string_view name)
{
    return physicsNumericArgCount(name) != std::numeric_limits<size_t>::max();
}

bool validatePhysicsFunctionArity(std::string_view name,
                                  size_t expected,
                                  size_t actual,
                                  std::string& reason)
{
    if (expected == actual)
        return true;
    reason = std::format("'{}' expects {} argument(s), got {}.",
                         std::string(name),
                         expected,
                         actual);
    return false;
}

bool evaluateBuiltInNumericFunction(std::string_view name,
                                    const std::vector<double>& args,
                                    double timeSeconds,
                                    double& value,
                                    std::string& reason)
{
    auto require = [&](size_t count) {
        if (args.size() == count)
            return true;
        reason = std::format("'{}' expects {} argument(s), got {}.",
                             std::string(name),
                             count,
                             args.size());
        return false;
    };

    if (name == "sin") { if (!require(1)) return false; value = std::sin(args[0]); return true; }
    if (name == "cos") { if (!require(1)) return false; value = std::cos(args[0]); return true; }
    if (name == "tan") { if (!require(1)) return false; value = std::tan(args[0]); return true; }
    if (name == "abs") { if (!require(1)) return false; value = std::abs(args[0]); return true; }
    if (name == "sqrt") {
        if (!require(1)) return false;
        if (args[0] < 0.0) {
            reason = "sqrt(value) does not accept a negative value.";
            return false;
        }
        value = std::sqrt(args[0]);
        return true;
    }
    if (name == "floor") { if (!require(1)) return false; value = std::floor(args[0]); return true; }
    if (name == "ceil") { if (!require(1)) return false; value = std::ceil(args[0]); return true; }
    if (name == "round") { if (!require(1)) return false; value = std::round(args[0]); return true; }
    if (name == "deg2rad") { if (!require(1)) return false; value = args[0] * 0.017453292519943295; return true; }
    if (name == "rad2deg") { if (!require(1)) return false; value = args[0] * 57.29577951308232; return true; }
    if (name == "min") { if (!require(2)) return false; value = std::min(args[0], args[1]); return true; }
    if (name == "max") { if (!require(2)) return false; value = std::max(args[0], args[1]); return true; }
    if (name == "pow") { if (!require(2)) return false; value = std::pow(args[0], args[1]); return true; }
    if (name == "clamp") {
        if (!require(3)) return false;
        const double lo = std::min(args[1], args[2]);
        const double hi = std::max(args[1], args[2]);
        value = std::clamp(args[0], lo, hi);
        return true;
    }
    if (name == "lerp") {
        if (!require(3)) return false;
        value = std::lerp(args[0], args[1], args[2]);
        return true;
    }
    if (name == "inverseLerp") {
        if (!require(3)) return false;
        const double span = args[1] - args[0];
        if (std::abs(span) < 1e-9) {
            reason = "inverseLerp(a, b, value) needs a and b to be different.";
            return false;
        }
        value = (args[2] - args[0]) / span;
        return true;
    }
    if (name == "smoothstep") {
        if (!require(3)) return false;
        const double span = args[1] - args[0];
        if (std::abs(span) < 1e-9) {
            reason = "smoothstep(edge0, edge1, value) needs different edge values.";
            return false;
        }
        const double t = std::clamp((args[2] - args[0]) / span, 0.0, 1.0);
        value = t * t * (3.0 - 2.0 * t);
        return true;
    }
    if (name == "repeat") {
        if (!require(2)) return false;
        const double length = std::abs(args[1]);
        if (length < 1e-9) {
            reason = "repeat(value, length) needs length greater than zero.";
            return false;
        }
        value = std::fmod(args[0], length);
        if (value < 0.0)
            value += length;
        return true;
    }
    if (name == "pingpong") {
        if (!require(2)) return false;
        const double length = std::abs(args[1]);
        if (length < 1e-9) {
            reason = "pingpong(value, length) needs length greater than zero.";
            return false;
        }
        double cycle = std::fmod(args[0], length * 2.0);
        if (cycle < 0.0)
            cycle += length * 2.0;
        value = length - std::abs(cycle - length);
        return true;
    }
    if (name == "wave" || name == "bob") {
        if (args.size() != 2 && args.size() != 3) {
            reason = std::format("'{}' expects speed, amplitude, and optional phase.", std::string(name));
            return false;
        }
        const double phase = args.size() == 3 ? args[2] : 0.0;
        value = std::sin(timeSeconds * args[0] + phase) * args[1];
        return true;
    }
    if (name == "pulse") {
        if (args.size() != 3 && args.size() != 4) {
            reason = "pulse(speed, min, max, phase?) expects 3 or 4 arguments.";
            return false;
        }
        const double phase = args.size() == 4 ? args[3] : 0.0;
        const double t = (std::sin(timeSeconds * args[0] + phase) + 1.0) * 0.5;
        value = std::lerp(args[1], args[2], t);
        return true;
    }

    return false;
}

class NumericExpressionParser {
public:
    using Resolver     = std::function<bool(std::string_view, double&, std::string&)>;
    using FuncResolver = std::function<bool(std::string_view, const std::vector<double>&, double&, std::string&)>;

    NumericExpressionParser(std::string_view text, size_t baseColumn, Resolver resolver, FuncResolver funcResolver = {})
        : m_text(text), m_baseColumn(baseColumn), m_resolver(std::move(resolver)), m_funcResolver(std::move(funcResolver)) {}

    bool parse(double& out, size_t& errorColumn, std::string& errorReason);

private:
    bool parseExpression(double& out);
    bool parseTerm(double& out);
    bool parseFactor(double& out);
    void skipWhitespace();
    bool match(char ch);
    void setError(std::string reason, size_t offset = static_cast<size_t>(-1));

    std::string_view m_text;
    size_t           m_baseColumn = 1;
    Resolver         m_resolver;
    FuncResolver     m_funcResolver;
    size_t           m_pos = 0;
    size_t           m_errorColumn = 1;
    std::string      m_errorReason;
};

bool validateNumericIdentifier(std::string_view identifier,
                               const ScriptBehaviorDefinition& behavior,
                               std::string_view eventName,
                               std::string& reason);
bool validateNumericExpression(std::string_view expression,
                               const ScriptBehaviorDefinition& behavior,
                               std::string_view eventName,
                               size_t baseColumn,
                               size_t& errorColumn,
                               std::string& errorReason);
bool validateStringExpression(std::string_view expression,
                              const ScriptBehaviorDefinition& behavior,
                              std::string_view eventName,
                              size_t& errorColumn,
                              std::string& errorReason);
bool validateBoolExpression(std::string_view expression,
                            const ScriptBehaviorDefinition& behavior,
                            size_t& errorColumn,
                            std::string& errorReason);
bool validateVec3Expression(std::string_view expression,
                            const ScriptBehaviorDefinition& behavior,
                            size_t& errorColumn,
                            std::string& errorReason);
bool validatePrintExpression(std::string_view expression,
                             const ScriptBehaviorDefinition& behavior,
                             std::string_view eventName,
                             size_t baseColumn,
                             size_t& errorColumn,
                             std::string& errorReason);

bool NumericExpressionParser::parse(double& out, size_t& errorColumn, std::string& errorReason)
{
    m_errorColumn = m_baseColumn;
    if (!parseExpression(out)) {
        errorColumn = m_errorColumn;
        errorReason = m_errorReason;
        return false;
    }

    skipWhitespace();
    if (m_pos != m_text.size()) {
        setError("Unexpected token in numeric expression.");
        errorColumn = m_errorColumn;
        errorReason = m_errorReason;
        return false;
    }

    errorColumn = 0;
    errorReason.clear();
    return true;
}

bool NumericExpressionParser::parseExpression(double& out)
{
    if (!parseTerm(out))
        return false;

    while (true) {
        skipWhitespace();
        if (match('+')) {
            double rhs = 0.0;
            if (!parseTerm(rhs))
                return false;
            out += rhs;
            continue;
        }
        if (match('-')) {
            double rhs = 0.0;
            if (!parseTerm(rhs))
                return false;
            out -= rhs;
            continue;
        }
        return true;
    }
}

bool NumericExpressionParser::parseTerm(double& out)
{
    if (!parseFactor(out))
        return false;

    while (true) {
        skipWhitespace();
        if (match('*')) {
            double rhs = 0.0;
            if (!parseFactor(rhs))
                return false;
            out *= rhs;
            continue;
        }
        if (match('/')) {
            double rhs = 0.0;
            if (!parseFactor(rhs))
                return false;
            if (rhs == 0.0) {
                setError("Division by zero is not allowed.");
                return false;
            }
            out /= rhs;
            continue;
        }
        return true;
    }
}

bool NumericExpressionParser::parseFactor(double& out)
{
    skipWhitespace();
    if (m_pos >= m_text.size()) {
        setError("Unexpected end of numeric expression.");
        return false;
    }

    if (match('+'))
        return parseFactor(out);

    if (match('-')) {
        if (!parseFactor(out))
            return false;
        out = -out;
        return true;
    }

    if (match('(')) {
        if (!parseExpression(out))
            return false;

        skipWhitespace();
        if (!match(')')) {
            setError("Missing closing ')' in numeric expression.");
            return false;
        }
        return true;
    }

    const size_t start = m_pos;
    if (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '.') {
        bool seenDigit = false;
        bool seenDot = false;
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos];
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                seenDigit = true;
                ++m_pos;
                continue;
            }
            if (ch == '.' && !seenDot) {
                seenDot = true;
                ++m_pos;
                continue;
            }
            break;
        }

        if (!seenDigit) {
            setError("Invalid numeric literal.");
            return false;
        }

        try {
            out = std::stod(std::string(m_text.substr(start, m_pos - start)));
            return true;
        } catch (...) {
            setError("Invalid numeric literal.");
            return false;
        }
    }

    if (std::isalpha(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '_') {
        ++m_pos;
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.')
                ++m_pos;
            else
                break;
        }

        const std::string identifier(m_text.substr(start, m_pos - start));

        // Check for function call: identifier(args...)
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '(') {
            if (!m_funcResolver) {
                setError(std::format("Function calls are not supported here.", identifier), start);
                return false;
            }
            ++m_pos; // consume '('
            std::vector<double> args;
            skipWhitespace();
            while (m_pos < m_text.size() && m_text[m_pos] != ')') {
                double arg = 0.0;
                if (!parseExpression(arg))
                    return false;
                args.push_back(arg);
                skipWhitespace();
                if (m_pos < m_text.size() && m_text[m_pos] == ',')
                    ++m_pos;
                skipWhitespace();
            }
            if (!match(')')) {
                setError("Missing closing ')' in function call.");
                return false;
            }
            std::string reason;
            if (!m_funcResolver(identifier, args, out, reason)) {
                setError(reason.empty() ? std::format("Unknown function '{}'.", identifier) : reason, start);
                return false;
            }
            return true;
        }

        std::string reason;
        if (!m_resolver(identifier, out, reason)) {
            setError(reason.empty()
                         ? std::format("Unknown identifier '{}'.", identifier)
                         : reason,
                     start);
            return false;
        }
        return true;
    }

    setError("Unexpected token in numeric expression.");
    return false;
}

void NumericExpressionParser::skipWhitespace()
{
    while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos])))
        ++m_pos;
}

bool NumericExpressionParser::match(char ch)
{
    if (m_pos < m_text.size() && m_text[m_pos] == ch) {
        ++m_pos;
        return true;
    }
    return false;
}

void NumericExpressionParser::setError(std::string reason, size_t offset)
{
    if (offset == static_cast<size_t>(-1))
        offset = m_pos;
    m_errorColumn = m_baseColumn + offset;
    m_errorReason = std::move(reason);
}

bool validateNumericIdentifier(std::string_view identifier,
                               const ScriptBehaviorDefinition& behavior,
                               std::string_view eventName,
                               std::string& reason)
{
    if (identifier == "deltaTime") {
        if (eventName == "on_update")
            return true;
        reason = "deltaTime is only available inside on_update(deltaTime: float).";
        return false;
    }

    if (identifier == "dt") {
        if (eventName == "on_update" || eventName.starts_with("func:"))
            return true;
        reason = "dt is only available inside on_update or functions called from on_update.";
        return false;
    }

    if (identifier == "time" || identifier == "runtime.time")
        return true;

    if (identifier == "other") {
        if (eventName == "on_trigger")
            return true;
        reason = "other is only available inside on_trigger.";
        return false;
    }

    if (parseTransformTarget(identifier) != ScriptTargetKind::None)
        return true;

    if (const ScriptFieldDefinition* field = findFieldDefinition(behavior, identifier)) {
        if (isNumericType(field->type))
            return true;
        reason = std::format("'{}' is a {} property, not a numeric value.",
                             std::string(identifier),
                             fieldTypeName(field->type));
        return false;
    }

    if (isSimpleIdentifier(identifier))
        return true;

    reason = std::format("Unknown identifier '{}'.", std::string(identifier));
    return false;
}

bool validateNumericExpression(std::string_view expression,
                               const ScriptBehaviorDefinition& behavior,
                               std::string_view eventName,
                               size_t baseColumn,
                               size_t& errorColumn,
                               std::string& errorReason)
{
    NumericExpressionParser parser(
        expression,
        baseColumn,
        [&](std::string_view identifier, double& out, std::string& reason) {
            out = 0.0;
            return validateNumericIdentifier(identifier, behavior, eventName, reason);
        },
        [&](std::string_view name, const std::vector<double>& args, double& out, std::string& reason) {
            out = 0.0;
            if (evaluateBuiltInNumericFunction(name, args, 0.0, out, reason))
                return true;

            if (isPhysicsNumericFunctionName(name))
                return validatePhysicsFunctionArity(name, physicsNumericArgCount(name), args.size(), reason);

            for (const ScriptCompiledFunc& func : behavior.funcs) {
                if (func.name == name && func.params.size() == args.size())
                    return true;
            }

            if (reason.empty()) {
                reason = std::format("Unknown function '{}' with {} argument(s).",
                                     std::string(name),
                                     args.size());
            }
            return false;
        });

    double value = 0.0;
    return parser.parse(value, errorColumn, errorReason);
}

bool validateStringExpression(std::string_view expression,
                              const ScriptBehaviorDefinition& behavior,
                              std::string_view eventName,
                              size_t& errorColumn,
                              std::string& errorReason)
{
    std::string literal;
    if (parseStringLiteral(expression, literal))
        return true;

    const std::string token = trimCopy(expression);
    if (token == "signal") {
        if (eventName == "on_signal")
            return true;
        errorColumn = 0;
        errorReason = "signal is only available inside on_signal.";
        return false;
    }

    if (const ScriptFieldDefinition* field = findFieldDefinition(behavior, token)) {
        if (field->type == ScriptFieldType::String)
            return true;
        errorColumn = 0;
        errorReason = std::format("'{}' is a {} property, not a string value.",
                                  token,
                                  fieldTypeName(field->type));
        return false;
    }

    errorColumn = 0;
    errorReason = "Expected a string literal or string property.";
    return false;
}

bool validateBoolExpression(std::string_view expression,
                            const ScriptBehaviorDefinition& behavior,
                            size_t& errorColumn,
                            std::string& errorReason)
{
    const std::string token = trimCopy(expression);
    if (token == "true" || token == "false")
        return true;

    std::string callName;
    std::vector<std::string> callArgs;
    if (parseFunctionCallText(token, callName, callArgs) &&
        (callName == "input_has" || callName == "input_touches"))
    {
        const size_t expected = callName == "input_has" ? 1 : 2;
        if (callArgs.size() != expected) {
            errorColumn = 0;
            errorReason = std::format("{} expects {} argument(s).", callName, expected);
            return false;
        }
        for (const std::string& arg : callArgs) {
            const std::string fieldName = trimCopy(arg);
            const ScriptFieldDefinition* field = findFieldDefinition(behavior, fieldName);
            if (!field || !isEntityFieldType(field->type)) {
                errorColumn = 0;
                errorReason = std::format("{} expects entity input fields, but '{}' is not one.", callName, fieldName);
                return false;
            }
        }
        return true;
    }

    if (const ScriptFieldDefinition* field = findFieldDefinition(behavior, token)) {
        if (field->type == ScriptFieldType::Bool)
            return true;
        errorColumn = 0;
        errorReason = std::format("'{}' is a {} property, not a bool value.",
                                  token,
                                  fieldTypeName(field->type));
        return false;
    }

    errorColumn = 0;
    errorReason = "Expected true, false, or a bool property.";
    return false;
}

bool validateVec3Expression(std::string_view expression,
                            const ScriptBehaviorDefinition& behavior,
                            size_t& errorColumn,
                            std::string& errorReason)
{
    glm::vec3 literal{};
    if (parseVec3Literal(expression, literal))
        return true;

    const std::string token = trimCopy(expression);
    if (isTransformVector(token))
        return true;

    if (const ScriptFieldDefinition* field = findFieldDefinition(behavior, token)) {
        if (field->type == ScriptFieldType::Vec3)
            return true;
        errorColumn = 0;
        errorReason = std::format("'{}' is a {} property, not a vec3 value.",
                                  token,
                                  fieldTypeName(field->type));
        return false;
    }

    errorColumn = 0;
    errorReason = "Expected a vec3 literal like {1, 2, 3} or a vec3 property.";
    return false;
}

bool validatePrintExpression(std::string_view expression,
                             const ScriptBehaviorDefinition& behavior,
                             std::string_view eventName,
                             size_t baseColumn,
                             size_t& errorColumn,
                             std::string& errorReason)
{
    std::string literal;
    if (parseStringLiteral(expression, literal))
        return true;

    const std::string token = trimCopy(expression);
    if (token == "true" || token == "false")
        return true;

    glm::vec3 vecLiteral{};
    if (parseVec3Literal(expression, vecLiteral))
        return true;

    if (isSimpleIdentifier(token)) {
        if (token == "signal" && eventName == "on_signal")
            return true;
        if (token == "other" && eventName == "on_trigger")
            return true;
        if (isTransformVector(token) || parseTransformTarget(token) != ScriptTargetKind::None)
            return true;
        if (const ScriptFieldDefinition* field = findFieldDefinition(behavior, token))
            return field->type != ScriptFieldType::None;
    }

    return validateNumericExpression(expression, behavior, eventName, baseColumn, errorColumn, errorReason);
}

bool validateSignalFunctionCall(std::string_view expression,
                                const ScriptBehaviorDefinition& behavior,
                                std::string_view eventName,
                                size_t baseColumn,
                                size_t& errorColumn,
                                std::string& errorReason)
{
    std::string name;
    std::vector<std::string> args;
    if (!parseFunctionCallText(expression, name, args) || !isSignalFunctionName(name))
        return false;

    auto validateSignalArg = [&](const std::string& arg) {
        size_t signalColumn = 0;
        std::string signalReason;
        if (validateStringExpression(arg, behavior, eventName, signalColumn, signalReason))
            return true;
        errorColumn = signalColumn > 0 ? signalColumn : baseColumn;
        errorReason = signalReason.empty() ? "Expected a string signal name." : signalReason;
        return false;
    };

    if (name == "emit" || name == "signal" || name == "broadcast") {
        if (args.size() != 1) {
            errorColumn = baseColumn;
            errorReason = std::format("{} expects exactly one string signal argument.", name);
            return false;
        }
        return validateSignalArg(args[0]);
    }

    if (name == "emitTo") {
        if (args.size() != 2) {
            errorColumn = baseColumn;
            errorReason = "emitTo expects targetEntityId and string signal arguments.";
            return false;
        }
        size_t targetColumn = 0;
        std::string targetReason;
        if (!validateNumericExpression(args[0], behavior, eventName, baseColumn, targetColumn, targetReason)) {
            errorColumn = targetColumn > 0 ? targetColumn : baseColumn;
            errorReason = targetReason.empty() ? "Invalid emitTo target entity id." : targetReason;
            return false;
        }
        return validateSignalArg(args[1]);
    }

    return false;
}

bool validateUIFunctionCall(std::string_view expression,
                            const ScriptBehaviorDefinition& behavior,
                            std::string_view eventName,
                            size_t baseColumn,
                            size_t& errorColumn,
                            std::string& errorReason)
{
    std::string name;
    std::vector<std::string> args;
    if (!parseFunctionCallText(expression, name, args) || !isUIActionFunctionName(name))
        return false;

    const size_t expected = uiActionArgCount(name);
    if (args.size() != expected) {
        errorColumn = baseColumn;
        errorReason = std::format("'{}' expects {} argument(s), got {}.",
                                  name,
                                  expected,
                                  args.size());
        return false;
    }

    const std::string target = trimCopy(args[0]);
    const ScriptFieldDefinition* field = findFieldDefinition(behavior, target);
    if (!field || !isEntityFieldType(field->type)) {
        errorColumn = baseColumn;
        errorReason = std::format("'{}' expects its first argument to be an entity input field.", name);
        return false;
    }

    if (name == "ui_set_text") {
        size_t textColumn = 0;
        std::string textReason;
        if (!validateStringExpression(args[1], behavior, eventName, textColumn, textReason)) {
            errorColumn = textColumn > 0 ? textColumn : baseColumn;
            errorReason = textReason.empty() ? "Expected a string for ui_set_text." : textReason;
            return false;
        }
        return true;
    }

    if (name == "ui_set_visible") {
        size_t boolColumn = 0;
        std::string boolReason;
        if (!validateBoolExpression(args[1], behavior, boolColumn, boolReason)) {
            errorColumn = boolColumn > 0 ? boolColumn : baseColumn;
            errorReason = boolReason.empty() ? "Expected true, false, or a bool property for ui_set_visible." : boolReason;
            return false;
        }
        return true;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        size_t argColumn = 0;
        std::string argReason;
        if (!validateNumericExpression(args[i], behavior, eventName, baseColumn, argColumn, argReason)) {
            errorColumn = argColumn > 0 ? argColumn : baseColumn;
            errorReason = argReason.empty() ? "Expected a numeric UI argument." : argReason;
            return false;
        }
    }

    return true;
}

bool validatePhysicsFunctionCall(std::string_view expression,
                                 const ScriptBehaviorDefinition& behavior,
                                 std::string_view eventName,
                                 size_t baseColumn,
                                 size_t& errorColumn,
                                 std::string& errorReason)
{
    std::string name;
    std::vector<std::string> args;
    if (!parseFunctionCallText(expression, name, args) || !isPhysicsActionFunctionName(name))
        return false;

    if (!validatePhysicsFunctionArity(name, physicsActionArgCount(name), args.size(), errorReason)) {
        errorColumn = baseColumn;
        return false;
    }

    for (const std::string& arg : args) {
        const std::string token = trimCopy(arg);
        if (token == "true" || token == "false")
            continue;

        size_t argColumn = 0;
        std::string argReason;
        if (!validateNumericExpression(arg, behavior, eventName, baseColumn, argColumn, argReason)) {
            errorColumn = argColumn > 0 ? argColumn : baseColumn;
            errorReason = argReason.empty() ? "Expected a numeric physics argument." : argReason;
            return false;
        }
    }

    return true;
}

bool evaluateNumericExpression(const ScriptExecutionContext& ctx,
                               std::string_view expression,
                               size_t baseColumn,
                               double& out,
                               std::string& reason);

bool evaluatePhysicsScalarArg(const ScriptExecutionContext& ctx,
                              const std::string& expression,
                              size_t baseColumn,
                              double& value,
                              std::string& reason)
{
    const std::string token = trimCopy(expression);
    if (token == "true") {
        value = 1.0;
        return true;
    }
    if (token == "false") {
        value = 0.0;
        return true;
    }
    return evaluateNumericExpression(ctx, expression, baseColumn, value, reason);
}

bool resolveNumericIdentifier(const ScriptExecutionContext& ctx,
                              std::string_view identifier,
                              double& out,
                              std::string& reason)
{
    // Local variables shadow everything else
    {
        auto it = ctx.localFloats.find(std::string(identifier));
        if (it != ctx.localFloats.end()) {
            out = it->second;
            return true;
        }
    }

    if (identifier == "deltaTime") {
        if (ctx.eventName == "on_update") {
            out = static_cast<double>(ctx.deltaTime);
            return true;
        }
        reason = "deltaTime is only available inside on_update(deltaTime: float).";
        return false;
    }

    if (identifier == "dt") {
        if (ctx.eventName == "on_update") {
            out = static_cast<double>(ctx.deltaTime);
            return true;
        }
        reason = "dt is only available inside on_update(deltaTime: float).";
        return false;
    }

    if (identifier == "time" || identifier == "runtime.time") {
        out = static_cast<double>(ctx.scene.getElapsedTime());
        return true;
    }

    if (identifier == "other") {
        if (ctx.eventName == "on_trigger") {
            try {
                out = std::stod(std::string(ctx.payload));
                return true;
            } catch (...) {
                reason = "other could not be converted into a numeric value.";
                return false;
            }
        }
        reason = "other is only available inside on_trigger.";
        return false;
    }

    switch (parseTransformTarget(identifier)) {
        case ScriptTargetKind::TransformTranslationX:
        case ScriptTargetKind::TransformTranslationY:
        case ScriptTargetKind::TransformTranslationZ:
        case ScriptTargetKind::TransformRotationX:
        case ScriptTargetKind::TransformRotationY:
        case ScriptTargetKind::TransformRotationZ:
        case ScriptTargetKind::TransformScaleX:
        case ScriptTargetKind::TransformScaleY:
        case ScriptTargetKind::TransformScaleZ: {
            TransformComponent* transform = ctx.scene.getComponent<TransformComponent>(ctx.entityId);
            if (!transform) {
                reason = "Entity is missing a Transform component.";
                return false;
            }

            switch (parseTransformTarget(identifier)) {
                case ScriptTargetKind::TransformTranslationX: out = transform->translation.x; return true;
                case ScriptTargetKind::TransformTranslationY: out = transform->translation.y; return true;
                case ScriptTargetKind::TransformTranslationZ: out = transform->translation.z; return true;
                case ScriptTargetKind::TransformRotationX:    out = transform->rotation.x;    return true;
                case ScriptTargetKind::TransformRotationY:    out = transform->rotation.y;    return true;
                case ScriptTargetKind::TransformRotationZ:    out = transform->rotation.z;    return true;
                case ScriptTargetKind::TransformScaleX:       out = transform->scale.x;       return true;
                case ScriptTargetKind::TransformScaleY:       out = transform->scale.y;       return true;
                case ScriptTargetKind::TransformScaleZ:       out = transform->scale.z;       return true;
                default:                                      break;
            }
            break;
        }
        default:
            break;
    }

    if (const ScriptFieldValue* field = findFieldValue(ctx.script, identifier)) {
        if (field->type == ScriptFieldType::Int) {
            out = static_cast<double>(field->intValue);
            return true;
        }
        if (field->type == ScriptFieldType::Float) {
            out = static_cast<double>(field->floatValue);
            return true;
        }

        reason = std::format("'{}' is a {} property, not a numeric value.",
                             std::string(identifier),
                             fieldTypeName(field->type));
        return false;
    }

    reason = std::format("Unknown identifier '{}'.", std::string(identifier));
    return false;
}

// Forward declarations needed for mutual recursion between evaluateNumericExpression and executeStatement
bool executeStatement(const ScriptCompiledStatement& statement, ScriptExecutionContext& ctx, std::string& reason);
void executeBlock(const std::vector<ScriptCompiledStatement>& block, ScriptExecutionContext& ctx);
bool evaluatePhysicsNumericFunction(const ScriptExecutionContext& ctx,
                                    std::string_view name,
                                    const std::vector<double>& args,
                                    double& value,
                                    std::string& reason);

bool evaluateNumericExpression(const ScriptExecutionContext& ctx,
                               std::string_view expression,
                               size_t baseColumn,
                               double& out,
                               std::string& reason)
{
    NumericExpressionParser parser(
        expression,
        baseColumn,
        [&](std::string_view identifier, double& value, std::string& resolveReason) {
            return resolveNumericIdentifier(ctx, identifier, value, resolveReason);
        },
        [&](std::string_view name, const std::vector<double>& args, double& value, std::string& resolveReason) -> bool {
            if (evaluateBuiltInNumericFunction(name, args, ctx.scene.getElapsedTime(), value, resolveReason))
                return true;
            if (evaluatePhysicsNumericFunction(ctx, name, args, value, resolveReason))
                return true;

            // User-defined func blocks
            for (const ScriptCompiledFunc& func : ctx.behavior.funcs) {
                if (func.name != name || func.params.size() != args.size())
                    continue;
                // Build a child context with params bound as local floats
                ScriptExecutionContext funcCtx{
                    ctx.scene, ctx.script, ctx.behavior,
                    ctx.entityId, ctx.deltaTime, ctx.eventName, ctx.payload,
                    {}, {}, {}, {}
                };
                for (size_t i = 0; i < func.params.size(); ++i)
                    funcCtx.localFloats[func.params[i].name] = args[i];
                // Copy caller locals so func can read outer vars (read-only shadow)
                for (const auto& [k, v] : ctx.localFloats)
                    if (!funcCtx.localFloats.count(k))
                        funcCtx.localFloats[k] = v;

                // Execute body — a Return statement throws to unwind
                struct ReturnValue { double v; };
                try {
                    for (const ScriptCompiledStatement& stmt : func.body) {
                        if (stmt.kind == ScriptStatementKind::Return) {
                            double retVal = 0.0;
                            std::string retReason;
                            if (evaluateNumericExpression(funcCtx, stmt.expression, stmt.column, retVal, retReason))
                                throw ReturnValue{retVal};
                            resolveReason = retReason;
                            return false;
                        }
                        std::string ignore;
                        executeStatement(stmt, funcCtx, ignore);
                    }
                } catch (const ReturnValue& rv) {
                    value = rv.v;
                    return true;
                }
                value = 0.0;
                return true;
            }

            resolveReason = std::format("Unknown function '{}' with {} argument(s).", std::string(name), args.size());
            return false;
        });

    size_t errorColumn = 0;
    if (!parser.parse(out, errorColumn, reason)) {
        if (errorColumn > 0)
            reason = std::format("col {}: {}", errorColumn, reason);
        return false;
    }
    return true;
}

bool resolveStringIdentifier(const ScriptExecutionContext& ctx,
                             std::string_view identifier,
                             std::string& out,
                             std::string& reason)
{
    // Local variables shadow props
    {
        auto it = ctx.localStrings.find(std::string(identifier));
        if (it != ctx.localStrings.end()) { out = it->second; return true; }
    }

    if (identifier == "signal") {
        if (ctx.eventName == "on_signal") {
            out = std::string(ctx.payload);
            return true;
        }
        reason = "signal is only available inside on_signal.";
        return false;
    }

    if (const ScriptFieldValue* field = findFieldValue(ctx.script, identifier)) {
        if (field->type == ScriptFieldType::String) {
            out = field->stringValue;
            return true;
        }

        reason = std::format("'{}' is a {} property, not a string value.",
                             std::string(identifier),
                             fieldTypeName(field->type));
        return false;
    }

    reason = "Expected a string literal or string property.";
    return false;
}

bool evaluateStringExpression(const ScriptExecutionContext& ctx,
                              std::string_view expression,
                              std::string& out,
                              std::string& reason)
{
    if (parseStringLiteral(expression, out))
        return true;

    return resolveStringIdentifier(ctx, trimCopy(expression), out, reason);
}

bool resolveBoolIdentifier(const ScriptExecutionContext& ctx,
                           std::string_view identifier,
                           bool& out,
                           std::string& reason)
{
    // Local variables shadow props
    {
        auto it = ctx.localBools.find(std::string(identifier));
        if (it != ctx.localBools.end()) { out = it->second; return true; }
    }

    if (const ScriptFieldValue* field = findFieldValue(ctx.script, identifier)) {
        if (field->type == ScriptFieldType::Bool) {
            out = field->boolValue;
            return true;
        }

        reason = std::format("'{}' is a {} property, not a bool value.",
                             std::string(identifier),
                             fieldTypeName(field->type));
        return false;
    }

    reason = "Expected true, false, or a bool property.";
    return false;
}

bool resolveEntityInputField(const ScriptExecutionContext& ctx,
                             std::string_view identifier,
                             EntityID& out,
                             std::string& reason)
{
    const ScriptFieldValue* field = findFieldValue(ctx.script, trimCopy(identifier));
    if (!field || !isEntityFieldType(field->type)) {
        reason = std::format("'{}' is not an entity input field.", std::string(identifier));
        return false;
    }
    out = field->entityValue;
    return true;
}

bool evaluateInputBoolFunction(const ScriptExecutionContext& ctx,
                               std::string_view expression,
                               bool& out,
                               std::string& reason)
{
    std::string name;
    std::vector<std::string> args;
    if (!parseFunctionCallText(expression, name, args))
        return false;

    if (name == "input_has") {
        if (args.size() != 1) {
            reason = "input_has expects one input field.";
            return true;
        }
        EntityID id = NULL_ENTITY;
        if (!resolveEntityInputField(ctx, args[0], id, reason))
            return true;
        out = id != NULL_ENTITY && ctx.scene.entityExists(id);
        return true;
    }

    if (name == "input_touches") {
        if (args.size() != 2) {
            reason = "input_touches expects two input fields.";
            return true;
        }

        EntityID a = NULL_ENTITY;
        EntityID b = NULL_ENTITY;
        if (!resolveEntityInputField(ctx, args[0], a, reason) ||
            !resolveEntityInputField(ctx, args[1], b, reason))
        {
            return true;
        }

        out = false;
        if (a == NULL_ENTITY || b == NULL_ENTITY)
            return true;
        for (const CollisionPair& pair : ctx.scene.getCollisionSystem().getPairs()) {
            if ((pair.a == a && pair.b == b) || (pair.a == b && pair.b == a)) {
                out = true;
                break;
            }
        }
        return true;
    }

    return false;
}

bool evaluateBoolExpression(const ScriptExecutionContext& ctx,
                            std::string_view expression,
                            bool& out,
                            std::string& reason)
{
    const std::string token = trimCopy(expression);
    if (token == "true") {
        out = true;
        return true;
    }
    if (token == "false") {
        out = false;
        return true;
    }

    if (evaluateInputBoolFunction(ctx, token, out, reason))
        return reason.empty();

    return resolveBoolIdentifier(ctx, token, out, reason);
}

bool resolveVec3Identifier(const ScriptExecutionContext& ctx,
                           std::string_view identifier,
                           glm::vec3& out,
                           std::string& reason)
{
    // Local variables shadow props
    {
        auto it = ctx.localVecs.find(std::string(identifier));
        if (it != ctx.localVecs.end()) { out = it->second; return true; }
    }

    if (identifier == "transform.translation" ||
        identifier == "transform.position" ||
        identifier == "transform.rotation" ||
        identifier == "transform.scale") {
        TransformComponent* transform = ctx.scene.getComponent<TransformComponent>(ctx.entityId);
        if (!transform) {
            reason = "Entity is missing a Transform component.";
            return false;
        }

        if (identifier == "transform.translation" || identifier == "transform.position") out = transform->translation;
        else if (identifier == "transform.rotation") out = transform->rotation;
        else out = transform->scale;
        return true;
    }

    if (const ScriptFieldValue* field = findFieldValue(ctx.script, identifier)) {
        if (field->type == ScriptFieldType::Vec3) {
            out = field->vec3Value;
            return true;
        }

        reason = std::format("'{}' is a {} property, not a vec3 value.",
                             std::string(identifier),
                             fieldTypeName(field->type));
        return false;
    }

    reason = "Expected a vec3 literal like {1, 2, 3} or a vec3 property.";
    return false;
}

bool evaluateVec3Expression(const ScriptExecutionContext& ctx,
                            std::string_view expression,
                            glm::vec3& out,
                            std::string& reason)
{
    if (parseVec3Literal(expression, out))
        return true;

    return resolveVec3Identifier(ctx, trimCopy(expression), out, reason);
}

bool evaluatePrintableExpression(const ScriptExecutionContext& ctx,
                                 std::string_view expression,
                                 size_t baseColumn,
                                 std::string& out,
                                 std::string& reason)
{
    if (parseStringLiteral(expression, out))
        return true;

    const std::string token = trimCopy(expression);
    if (token == "true" || token == "false") {
        out = token;
        return true;
    }

    glm::vec3 vecValue{};
    if (parseVec3Literal(expression, vecValue)) {
        out = glm::to_string(vecValue);
        return true;
    }

    if (isSimpleIdentifier(token)) {
        std::string stringValue;
        if (resolveStringIdentifier(ctx, token, stringValue, reason)) {
            out = stringValue;
            return true;
        }

        bool boolValue = false;
        if (resolveBoolIdentifier(ctx, token, boolValue, reason)) {
            out = boolValue ? "true" : "false";
            return true;
        }

        if (resolveVec3Identifier(ctx, token, vecValue, reason)) {
            out = glm::to_string(vecValue);
            return true;
        }
    }

    double numericValue = 0.0;
    if (evaluateNumericExpression(ctx, expression, baseColumn, numericValue, reason)) {
        out = std::format("{}", numericValue);
        return true;
    }

    return false;
}

bool executeSignalFunctionCall(std::string_view expression,
                               ScriptExecutionContext& ctx,
                               size_t baseColumn,
                               std::string& reason)
{
    std::string name;
    std::vector<std::string> args;
    if (!parseFunctionCallText(expression, name, args) || !isSignalFunctionName(name))
        return false;

    auto evalSignal = [&](const std::string& expressionText, std::string& outSignal) {
        if (evaluateStringExpression(ctx, expressionText, outSignal, reason))
            return true;
        reason = reason.empty() ? "Expected a string signal name." : reason;
        return false;
    };

    if (name == "emit" || name == "signal") {
        if (args.size() != 1) {
            reason = std::format("{} expects exactly one string signal argument.", name);
            return false;
        }
        std::string signalName;
        if (!evalSignal(args[0], signalName))
            return false;
        ScriptEngine::get().sendSignal(ctx.scene, ctx.entityId, signalName);
        return true;
    }

    if (name == "broadcast") {
        if (args.size() != 1) {
            reason = "broadcast expects exactly one string signal argument.";
            return false;
        }
        std::string signalName;
        if (!evalSignal(args[0], signalName))
            return false;
        ScriptEngine::get().sendSignalToAll(ctx.scene, signalName);
        return true;
    }

    if (name == "emitTo") {
        if (args.size() != 2) {
            reason = "emitTo expects targetEntityId and string signal arguments.";
            return false;
        }
        double target = 0.0;
        if (!evaluateNumericExpression(ctx, args[0], baseColumn, target, reason))
            return false;
        std::string signalName;
        if (!evalSignal(args[1], signalName))
            return false;
        if (target <= 0.0) {
            reason = "emitTo target entity id must be greater than zero.";
            return false;
        }
        ScriptEngine::get().sendSignal(ctx.scene, static_cast<EntityID>(target), signalName);
        return true;
    }

    return false;
}

bool executeUIFunctionCall(std::string_view expression,
                           ScriptExecutionContext& ctx,
                           size_t baseColumn,
                           std::string& reason)
{
    std::string name;
    std::vector<std::string> args;
    if (!parseFunctionCallText(expression, name, args) || !isUIActionFunctionName(name))
        return false;

    const size_t expected = uiActionArgCount(name);
    if (args.size() != expected) {
        reason = std::format("'{}' expects {} argument(s), got {}.",
                             name,
                             expected,
                             args.size());
        return false;
    }

    EntityID target = NULL_ENTITY;
    if (!resolveEntityInputField(ctx, args[0], target, reason))
        return false;
    if (target == NULL_ENTITY || !ctx.scene.entityExists(target)) {
        reason = std::format("'{}' target input field is empty or points to a missing entity.", name);
        return false;
    }

    UIElementComponent* ui = ctx.scene.getComponent<UIElementComponent>(target);
    if (!ui) {
        reason = std::format("'{}' target entity does not have a UI Element component.", name);
        return false;
    }

    auto syncRenderProxy = [&]() {
        if (MeshRendererComponent* mesh = ctx.scene.getComponent<MeshRendererComponent>(target))
            mesh->visible = ui->visible;
        if (MaterialComponent* material = ctx.scene.getComponent<MaterialComponent>(target)) {
            material->albedoColor = ui->color;
            material->albedoTexture = ui->imagePath;
            material->doubleSided = true;
            material->alphaBlend = true;
            material->dirty = true;
        }
    };

    if (name == "ui_show") {
        ui->visible = true;
        syncRenderProxy();
        return true;
    }
    if (name == "ui_hide") {
        ui->visible = false;
        syncRenderProxy();
        return true;
    }
    if (name == "ui_toggle") {
        ui->visible = !ui->visible;
        syncRenderProxy();
        return true;
    }
    if (name == "ui_set_visible") {
        bool value = false;
        if (!evaluateBoolExpression(ctx, args[1], value, reason))
            return false;
        ui->visible = value;
        syncRenderProxy();
        return true;
    }
    if (name == "ui_set_text") {
        std::string value;
        if (!evaluateStringExpression(ctx, args[1], value, reason))
            return false;
        ui->text = value;
        return true;
    }

    auto evalNumber = [&](size_t argIndex, double& value) {
        return evaluateNumericExpression(ctx, args[argIndex], baseColumn, value, reason);
    };

    if (name == "ui_set_color") {
        double r = 1.0;
        double g = 1.0;
        double b = 1.0;
        double a = 1.0;
        if (!evalNumber(1, r) || !evalNumber(2, g) || !evalNumber(3, b) || !evalNumber(4, a))
            return false;
        ui->color = {
            static_cast<float>(std::clamp(r, 0.0, 1.0)),
            static_cast<float>(std::clamp(g, 0.0, 1.0)),
            static_cast<float>(std::clamp(b, 0.0, 1.0)),
            static_cast<float>(std::clamp(a, 0.0, 1.0)),
        };
        syncRenderProxy();
        return true;
    }
    if (name == "ui_set_size") {
        double width = 0.0;
        double height = 0.0;
        if (!evalNumber(1, width) || !evalNumber(2, height))
            return false;
        ui->size = {
            static_cast<float>(std::max(width, 1.0)),
            static_cast<float>(std::max(height, 1.0)),
        };
        return true;
    }

    return false;
}

RigidBodyComponent* getOrCreateRigidBody(ScriptExecutionContext& ctx)
{
    if (RigidBodyComponent* body = ctx.scene.getComponent<RigidBodyComponent>(ctx.entityId))
        return body;
    return ctx.scene.addComponent(ctx.entityId, RigidBodyComponent{});
}

BoxColliderComponent* getOrCreateBoxCollider(ScriptExecutionContext& ctx)
{
    if (BoxColliderComponent* collider = ctx.scene.getComponent<BoxColliderComponent>(ctx.entityId))
        return collider;
    return ctx.scene.addComponent(ctx.entityId, BoxColliderComponent{});
}

bool evaluatePhysicsNumericFunction(const ScriptExecutionContext& ctx,
                                    std::string_view name,
                                    const std::vector<double>& args,
                                    double& value,
                                    std::string& reason)
{
    if (!isPhysicsNumericFunctionName(name))
        return false;
    if (!validatePhysicsFunctionArity(name, physicsNumericArgCount(name), args.size(), reason))
        return false;

    const RigidBodyComponent* body = ctx.scene.getComponent<RigidBodyComponent>(ctx.entityId);
    const glm::vec3 velocity = body ? body->linearVelocity : glm::vec3(0.0f);

    if (name == "physics_velocity_x") { value = velocity.x; return true; }
    if (name == "physics_velocity_y") { value = velocity.y; return true; }
    if (name == "physics_velocity_z") { value = velocity.z; return true; }
    if (name == "physics_speed") {
        value = glm::length(velocity);
        return true;
    }
    if (name == "physics_grounded") {
        const TransformComponent* transform = ctx.scene.getComponent<TransformComponent>(ctx.entityId);
        if (!transform) {
            value = 0.0;
            return true;
        }

        const BoxColliderComponent* collider = ctx.scene.getComponent<BoxColliderComponent>(ctx.entityId);
        const glm::vec3 halfExtents = collider
            ? glm::abs(transform->scale) * collider->halfExtents
            : glm::vec3(0.35f, 0.9f, 0.35f);
        const glm::vec3 offset = collider ? collider->offset : glm::vec3(0.0f, -0.9f, 0.0f);
        const glm::vec3 center = transform->translation + offset;
        const float bottom = center.y - halfExtents.y;
        const float ground = ctx.scene.sampleGroundAtWorld(center.x, center.z, bottom + 0.35f, 0.45f);
        value = (ground > std::numeric_limits<float>::lowest() && std::abs(bottom - ground) <= 0.08f) ? 1.0 : 0.0;
        return true;
    }

    return false;
}

bool executePhysicsFunctionCall(std::string_view expression,
                                ScriptExecutionContext& ctx,
                                size_t baseColumn,
                                std::string& reason)
{
    std::string name;
    std::vector<std::string> argTexts;
    if (!parseFunctionCallText(expression, name, argTexts) || !isPhysicsActionFunctionName(name))
        return false;

    if (!validatePhysicsFunctionArity(name, physicsActionArgCount(name), argTexts.size(), reason))
        return false;

    std::vector<double> args;
    args.reserve(argTexts.size());
    for (const std::string& argText : argTexts) {
        double value = 0.0;
        if (!evaluatePhysicsScalarArg(ctx, argText, baseColumn, value, reason))
            return false;
        args.push_back(value);
    }

    auto vecArg = [&args]() {
        return glm::vec3(static_cast<float>(args[0]),
                         static_cast<float>(args[1]),
                         static_cast<float>(args[2]));
    };

    if (name == "physics_clear_velocity") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx)) {
            body->linearVelocity = {0.0f, 0.0f, 0.0f};
            body->angularVelocity = {0.0f, 0.0f, 0.0f};
        }
        return true;
    }

    if (name == "physics_set_velocity") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx))
            body->linearVelocity = vecArg();
        return true;
    }
    if (name == "physics_add_velocity") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx))
            body->linearVelocity += vecArg();
        return true;
    }
    if (name == "physics_add_force") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx)) {
            const float invMass = 1.0f / std::max(body->mass, 0.001f);
            body->linearVelocity += vecArg() * invMass * std::max(ctx.deltaTime, 0.0f);
        }
        return true;
    }
    if (name == "physics_add_impulse") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx)) {
            const float invMass = 1.0f / std::max(body->mass, 0.001f);
            body->linearVelocity += vecArg() * invMass;
        }
        return true;
    }
    if (name == "physics_set_gravity") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx))
            body->useGravity = args[0] != 0.0;
        return true;
    }
    if (name == "physics_set_gravity_scale") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx))
            body->gravityScale = static_cast<float>(args[0]);
        return true;
    }
    if (name == "physics_set_mass") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx))
            body->mass = std::max(static_cast<float>(args[0]), 0.001f);
        return true;
    }
    if (name == "physics_set_kinematic") {
        if (RigidBodyComponent* body = getOrCreateRigidBody(ctx)) {
            body->isKinematic = args[0] != 0.0;
            body->type = body->isKinematic ? BodyType::Kinematic : BodyType::Dynamic;
        }
        return true;
    }
    if (name == "physics_set_trigger") {
        if (BoxColliderComponent* collider = getOrCreateBoxCollider(ctx))
            collider->isTrigger = args[0] != 0.0;
        return true;
    }
    if (name == "physics_set_friction") {
        if (BoxColliderComponent* collider = getOrCreateBoxCollider(ctx))
            collider->friction = std::max(static_cast<float>(args[0]), 0.0f);
        return true;
    }
    if (name == "physics_set_restitution") {
        if (BoxColliderComponent* collider = getOrCreateBoxCollider(ctx))
            collider->restitution = std::clamp(static_cast<float>(args[0]), 0.0f, 1.0f);
        return true;
    }
    if (name == "physics_set_box") {
        if (BoxColliderComponent* collider = getOrCreateBoxCollider(ctx))
            collider->halfExtents = glm::max(glm::abs(vecArg()), glm::vec3(0.001f));
        return true;
    }
    if (name == "physics_set_collider_offset") {
        if (BoxColliderComponent* collider = getOrCreateBoxCollider(ctx))
            collider->offset = vecArg();
        return true;
    }

    return false;
}

bool readNumericTarget(const ScriptExecutionContext& ctx,
                       ScriptTargetKind targetKind,
                       std::string_view targetName,
                       double& out,
                       std::string& reason)
{
    switch (targetKind) {
        case ScriptTargetKind::TransformTranslationX:
        case ScriptTargetKind::TransformTranslationY:
        case ScriptTargetKind::TransformTranslationZ:
        case ScriptTargetKind::TransformRotationX:
        case ScriptTargetKind::TransformRotationY:
        case ScriptTargetKind::TransformRotationZ:
        case ScriptTargetKind::TransformScaleX:
        case ScriptTargetKind::TransformScaleY:
        case ScriptTargetKind::TransformScaleZ: {
            TransformComponent* transform = ctx.scene.getComponent<TransformComponent>(ctx.entityId);
            if (!transform) {
                reason = "Entity is missing a Transform component.";
                return false;
            }

            switch (targetKind) {
                case ScriptTargetKind::TransformTranslationX: out = transform->translation.x; return true;
                case ScriptTargetKind::TransformTranslationY: out = transform->translation.y; return true;
                case ScriptTargetKind::TransformTranslationZ: out = transform->translation.z; return true;
                case ScriptTargetKind::TransformRotationX:    out = transform->rotation.x;    return true;
                case ScriptTargetKind::TransformRotationY:    out = transform->rotation.y;    return true;
                case ScriptTargetKind::TransformRotationZ:    out = transform->rotation.z;    return true;
                case ScriptTargetKind::TransformScaleX:       out = transform->scale.x;       return true;
                case ScriptTargetKind::TransformScaleY:       out = transform->scale.y;       return true;
                case ScriptTargetKind::TransformScaleZ:       out = transform->scale.z;       return true;
                default:                                      break;
            }
            break;
        }

        case ScriptTargetKind::Field: {
            const ScriptFieldValue* field = findFieldValue(ctx.script, targetName);
            if (!field) {
                reason = std::format("Property '{}' was not found at runtime.", std::string(targetName));
                return false;
            }
            if (field->type == ScriptFieldType::Int) {
                out = static_cast<double>(field->intValue);
                return true;
            }
            if (field->type == ScriptFieldType::Float) {
                out = static_cast<double>(field->floatValue);
                return true;
            }

            reason = std::format("Property '{}' is not numeric.", std::string(targetName));
            return false;
        }

        default:
            break;
    }

    reason = "Unsupported numeric target.";
    return false;
}

bool writeNumericTarget(const ScriptExecutionContext& ctx,
                        ScriptTargetKind targetKind,
                        std::string_view targetName,
                        ScriptFieldType valueType,
                        double value,
                        std::string& reason)
{
    switch (targetKind) {
        case ScriptTargetKind::TransformTranslationX:
        case ScriptTargetKind::TransformTranslationY:
        case ScriptTargetKind::TransformTranslationZ:
        case ScriptTargetKind::TransformRotationX:
        case ScriptTargetKind::TransformRotationY:
        case ScriptTargetKind::TransformRotationZ:
        case ScriptTargetKind::TransformScaleX:
        case ScriptTargetKind::TransformScaleY:
        case ScriptTargetKind::TransformScaleZ: {
            TransformComponent* transform = ctx.scene.getComponent<TransformComponent>(ctx.entityId);
            if (!transform) {
                reason = "Entity is missing a Transform component.";
                return false;
            }

            const float floatValue = static_cast<float>(value);
            switch (targetKind) {
                case ScriptTargetKind::TransformTranslationX: transform->translation.x = floatValue; return true;
                case ScriptTargetKind::TransformTranslationY: transform->translation.y = floatValue; return true;
                case ScriptTargetKind::TransformTranslationZ: transform->translation.z = floatValue; return true;
                case ScriptTargetKind::TransformRotationX:    transform->rotation.x    = floatValue; return true;
                case ScriptTargetKind::TransformRotationY:    transform->rotation.y    = floatValue; return true;
                case ScriptTargetKind::TransformRotationZ:    transform->rotation.z    = floatValue; return true;
                case ScriptTargetKind::TransformScaleX:       transform->scale.x       = floatValue; return true;
                case ScriptTargetKind::TransformScaleY:       transform->scale.y       = floatValue; return true;
                case ScriptTargetKind::TransformScaleZ:       transform->scale.z       = floatValue; return true;
                default:                                      break;
            }
            break;
        }

        case ScriptTargetKind::Field: {
            ScriptFieldValue* field = findFieldValue(ctx.script, targetName);
            if (!field) {
                reason = std::format("Property '{}' was not found at runtime.", std::string(targetName));
                return false;
            }

            if (valueType == ScriptFieldType::Int) {
                field->intValue = static_cast<int64_t>(value);
                return true;
            }
            if (valueType == ScriptFieldType::Float) {
                field->floatValue = static_cast<float>(value);
                return true;
            }

            reason = std::format("Property '{}' is not numeric.", std::string(targetName));
            return false;
        }

        default:
            break;
    }

    reason = "Unsupported numeric target.";
    return false;
}

bool writeStringTarget(const ScriptExecutionContext& ctx,
                       std::string_view targetName,
                       const std::string& value,
                       std::string& reason)
{
    ScriptFieldValue* field = findFieldValue(ctx.script, targetName);
    if (!field) {
        reason = std::format("Property '{}' was not found at runtime.", std::string(targetName));
        return false;
    }
    if (field->type != ScriptFieldType::String) {
        reason = std::format("Property '{}' is not a string property.", std::string(targetName));
        return false;
    }
    field->stringValue = value;
    return true;
}

bool writeBoolTarget(const ScriptExecutionContext& ctx,
                     std::string_view targetName,
                     bool value,
                     std::string& reason)
{
    ScriptFieldValue* field = findFieldValue(ctx.script, targetName);
    if (!field) {
        reason = std::format("Property '{}' was not found at runtime.", std::string(targetName));
        return false;
    }
    if (field->type != ScriptFieldType::Bool) {
        reason = std::format("Property '{}' is not a bool property.", std::string(targetName));
        return false;
    }
    field->boolValue = value;
    return true;
}

bool writeVec3Target(const ScriptExecutionContext& ctx,
                     std::string_view targetName,
                     const glm::vec3& value,
                     std::string& reason)
{
    ScriptFieldValue* field = findFieldValue(ctx.script, targetName);
    if (!field) {
        reason = std::format("Property '{}' was not found at runtime.", std::string(targetName));
        return false;
    }
    if (field->type != ScriptFieldType::Vec3) {
        reason = std::format("Property '{}' is not a vec3 property.", std::string(targetName));
        return false;
    }
    field->vec3Value = value;
    return true;
}

void executeBlock(const std::vector<ScriptCompiledStatement>& block, ScriptExecutionContext& ctx)
{
    for (const ScriptCompiledStatement& stmt : block) {
        std::string ignore;
        executeStatement(stmt, ctx, ignore);
    }
}

bool executeStatement(const ScriptCompiledStatement& statement,
                      ScriptExecutionContext& ctx,
                      std::string& reason)
{
    // VarDecl: var name: type = expr
    if (statement.kind == ScriptStatementKind::VarDecl) {
        if (statement.valueType == ScriptFieldType::Float || statement.valueType == ScriptFieldType::Int) {
            double val = 0.0;
            if (!evaluateNumericExpression(ctx, statement.expression, statement.column, val, reason))
                return false;
            ctx.localFloats[statement.targetName] = val;
            return true;
        }
        if (statement.valueType == ScriptFieldType::String) {
            std::string val;
            if (!evaluateStringExpression(ctx, statement.expression, val, reason))
                return false;
            ctx.localStrings[statement.targetName] = val;
            return true;
        }
        if (statement.valueType == ScriptFieldType::Bool) {
            bool val = false;
            if (!evaluateBoolExpression(ctx, statement.expression, val, reason))
                return false;
            ctx.localBools[statement.targetName] = val;
            return true;
        }
        if (statement.valueType == ScriptFieldType::Vec3) {
            glm::vec3 val{};
            if (!evaluateVec3Expression(ctx, statement.expression, val, reason))
                return false;
            ctx.localVecs[statement.targetName] = val;
            return true;
        }
        reason = "Unsupported var type.";
        return false;
    }

    // IfStmt: if (cond) { thenBlock } [else { elseBlock }]
    if (statement.kind == ScriptStatementKind::IfStmt) {
        bool condition = false;
        std::string condReason;
        // Try bool literal/prop first
        if (!evaluateBoolExpression(ctx, statement.expression, condition, condReason)) {
            // Fall back to numeric (non-zero = true)
            double numVal = 0.0;
            if (!evaluateNumericExpression(ctx, statement.expression, statement.column, numVal, reason))
                return false;
            condition = numVal != 0.0;
        }
        const auto& branch = condition ? statement.thenBlock : statement.elseBlock;
        executeBlock(branch, ctx);
        return true;
    }

    // Return — handled by the func executor via exception; treated as no-op at top level
    if (statement.kind == ScriptStatementKind::Return)
        return true;

    // FuncCall: standalone call, return value discarded
    if (statement.kind == ScriptStatementKind::FuncCall) {
        if (executeSignalFunctionCall(statement.expression, ctx, statement.column, reason))
            return true;
        if (executeUIFunctionCall(statement.expression, ctx, statement.column, reason))
            return true;
        if (executePhysicsFunctionCall(statement.expression, ctx, statement.column, reason))
            return true;
        double ignored = 0.0;
        return evaluateNumericExpression(ctx, statement.expression, statement.column, ignored, reason);
    }
    if (statement.kind == ScriptStatementKind::Print) {
        std::string output;
        if (!evaluatePrintableExpression(ctx, statement.expression, statement.column, output, reason))
            return false;

        DEMON_LOG_INFO("[SCRIPT] {}", output);
        return true;
    }

    if (statement.valueType == ScriptFieldType::Int || statement.valueType == ScriptFieldType::Float) {
        double rhs = 0.0;
        if (!evaluateNumericExpression(ctx, statement.expression, statement.column, rhs, reason))
            return false;

        double finalValue = rhs;
        if (statement.assignOp != ScriptAssignOp::Set) {
            double currentValue = 0.0;
            if (!readNumericTarget(ctx, statement.targetKind, statement.targetName, currentValue, reason))
                return false;

            switch (statement.assignOp) {
                case ScriptAssignOp::Add:      finalValue = currentValue + rhs; break;
                case ScriptAssignOp::Subtract: finalValue = currentValue - rhs; break;
                case ScriptAssignOp::Multiply: finalValue = currentValue * rhs; break;
                case ScriptAssignOp::Divide:
                    if (rhs == 0.0) {
                        reason = "Division by zero is not allowed.";
                        return false;
                    }
                    finalValue = currentValue / rhs;
                    break;
                case ScriptAssignOp::Set:
                    break;
            }
        }

        return writeNumericTarget(ctx,
                                  statement.targetKind,
                                  statement.targetName,
                                  statement.valueType,
                                  finalValue,
                                  reason);
    }

    if (statement.valueType == ScriptFieldType::String) {
        std::string value;
        if (!evaluateStringExpression(ctx, statement.expression, value, reason))
            return false;
        return writeStringTarget(ctx, statement.targetName, value, reason);
    }

    if (statement.valueType == ScriptFieldType::Bool) {
        bool value = false;
        if (!evaluateBoolExpression(ctx, statement.expression, value, reason))
            return false;
        return writeBoolTarget(ctx, statement.targetName, value, reason);
    }

    if (statement.valueType == ScriptFieldType::Vec3) {
        glm::vec3 value{};
        if (!evaluateVec3Expression(ctx, statement.expression, value, reason))
            return false;
        return writeVec3Target(ctx, statement.targetName, value, reason);
    }

    reason = "Unsupported assignment target type.";
    return false;
}

bool compileStatement(const PendingStatement& pending,
                      const ScriptBehaviorDefinition& behavior,
                      const std::filesystem::path& path,
                      ScriptCompiledStatement& outStatement,
                      ScriptDiagnostic& outDiagnostic)
{
    static const std::regex printRegex(R"(^print\s*\(\s*(.+?)\s*\)\s*;?$)");
    static const std::regex assignRegex(R"(^([A-Za-z_][A-Za-z0-9_\.]*)\s*(\+=|-=|\*=|/=|=)\s*(.+?)\s*;?$)");
    static const std::regex varDeclRegex(
        R"(^var\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([A-Za-z_][A-Za-z0-9_]*))?\s*=\s*(.+?)\s*;?$)");
    static const std::regex returnRegex(
        R"(^return\s+(.+?)\s*;?$)");
    static const std::regex funcCallRegex(
        R"(^([A-Za-z_][A-Za-z0-9_]*\s*\(.*\))\s*;?$)");

    std::smatch match;
    
    // var declaration
    if (std::regex_match(pending.text, match, varDeclRegex)) {
        const std::string varName   = match[1].str();
        const std::string typeStr   = trimCopy(match[2].str());
        const std::string expression = trimCopy(match[3].str());

        ScriptFieldType type = ScriptFieldType::Float; // default
        if (!typeStr.empty()) {
            type = parseFieldTypeToken(typeStr);
            if (type == ScriptFieldType::None) {
                outDiagnostic = { path, pending.line, pending.column,
                    std::format("Unknown type '{}' in var declaration.", typeStr) };
                return false;
            }
        }

        size_t errorColumn = 0;
        std::string errorReason;
        bool valid = false;
        switch (type) {
            case ScriptFieldType::Int:
            case ScriptFieldType::Float:
                valid = validateNumericExpression(expression,
                                                  behavior,
                                                  pending.eventName,
                                                  pending.column,
                                                  errorColumn,
                                                  errorReason);
                break;
            case ScriptFieldType::String:
                valid = validateStringExpression(expression,
                                                 behavior,
                                                 pending.eventName,
                                                 errorColumn,
                                                 errorReason);
                break;
            case ScriptFieldType::Bool:
                valid = validateBoolExpression(expression,
                                               behavior,
                                               errorColumn,
                                               errorReason);
                break;
            case ScriptFieldType::Vec3:
                valid = validateVec3Expression(expression,
                                               behavior,
                                               errorColumn,
                                               errorReason);
                break;
            default:
                break;
        }

        if (!valid) {
            outDiagnostic = {
                path,
                pending.line,
                errorColumn > 0 ? errorColumn : pending.column,
                errorReason.empty() ? "Invalid variable initializer." : errorReason,
            };
            return false;
        }

        outStatement.kind       = ScriptStatementKind::VarDecl;
        outStatement.targetName = varName;
        outStatement.valueType  = type;
        outStatement.expression = expression;
        outStatement.line       = pending.line;
        outStatement.column     = pending.column;
        return true;
    }

    if (!startsWithKeyword(pending.text, "print") && std::regex_match(pending.text, match, funcCallRegex)) {
        const std::string expression = trimCopy(match[1].str());
        const size_t expressionOffset = pending.text.find(expression);
        const size_t expressionColumn = pending.column + expressionOffset;

        size_t errorColumn = 0;
        std::string errorReason;
        if (validateSignalFunctionCall(expression,
                                       behavior,
                                       pending.eventName,
                                       expressionColumn,
                                       errorColumn,
                                       errorReason)) {
            outStatement.kind = ScriptStatementKind::FuncCall;
            outStatement.line = pending.line;
            outStatement.column = expressionColumn;
            outStatement.expression = expression;
            return true;
        }

        if (validateUIFunctionCall(expression,
                                   behavior,
                                   pending.eventName,
                                   expressionColumn,
                                   errorColumn,
                                   errorReason)) {
            outStatement.kind = ScriptStatementKind::FuncCall;
            outStatement.line = pending.line;
            outStatement.column = expressionColumn;
            outStatement.expression = expression;
            return true;
        }

        if (validatePhysicsFunctionCall(expression,
                                        behavior,
                                        pending.eventName,
                                        expressionColumn,
                                        errorColumn,
                                        errorReason)) {
            outStatement.kind = ScriptStatementKind::FuncCall;
            outStatement.line = pending.line;
            outStatement.column = expressionColumn;
            outStatement.expression = expression;
            return true;
        }

        std::string callName;
        std::vector<std::string> callArgs;
        if (parseFunctionCallText(expression, callName, callArgs) && isSignalFunctionName(callName)) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = errorColumn > 0 ? errorColumn : expressionColumn,
                .reason = errorReason.empty() ? "Invalid DemonScript signal call." : errorReason,
            };
            return false;
        }
        if (isPhysicsActionFunctionName(callName)) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = errorColumn > 0 ? errorColumn : expressionColumn,
                .reason = errorReason.empty() ? "Invalid DemonScript physics call." : errorReason,
            };
            return false;
        }
        if (isUIActionFunctionName(callName)) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = errorColumn > 0 ? errorColumn : expressionColumn,
                .reason = errorReason.empty() ? "Invalid DemonScript UI call." : errorReason,
            };
            return false;
        }

        if (!validateNumericExpression(expression,
                                       behavior,
                                       pending.eventName,
                                       expressionColumn,
                                       errorColumn,
                                       errorReason)) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = errorColumn > 0 ? errorColumn : expressionColumn,
                .reason = errorReason.empty() ? "Invalid function call." : errorReason,
            };
            return false;
        }

        outStatement.kind = ScriptStatementKind::FuncCall;
        outStatement.line = pending.line;
        outStatement.column = expressionColumn;
        outStatement.expression = expression;
        return true;
    }

    // return statement
    if (std::regex_match(pending.text, match, returnRegex)) {
        outStatement.kind       = ScriptStatementKind::Return;
        outStatement.expression = trimCopy(match[1].str());
        outStatement.valueType  = ScriptFieldType::Float;
        outStatement.line       = pending.line;
        outStatement.column     = pending.column;
        return true;
    }
    
    if (std::regex_match(pending.text, match, printRegex)) {
        const std::string expression = trimCopy(match[1].str());
        const size_t expressionOffset = pending.text.find(expression);
        const size_t expressionColumn = pending.column + expressionOffset;

        size_t errorColumn = 0;
        std::string errorReason;
        if (!validatePrintExpression(expression,
                                     behavior,
                                     pending.eventName,
                                     expressionColumn,
                                     errorColumn,
                                     errorReason)) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = errorColumn > 0 ? errorColumn : expressionColumn,
                .reason = errorReason.empty() ? "Invalid print expression." : errorReason,
            };
            return false;
        }

        outStatement.kind = ScriptStatementKind::Print;
        outStatement.line = pending.line;
        outStatement.column = expressionColumn;
        outStatement.expression = expression;
        return true;
    }

    if (std::regex_match(pending.text, match, assignRegex)) {
        const std::string target = trimCopy(match[1].str());
        const std::string op = match[2].str();
        const std::string expression = trimCopy(match[3].str());
        const size_t expressionOffset = pending.text.find(expression);
        const size_t expressionColumn = pending.column + expressionOffset;

        ScriptFieldType valueType = ScriptFieldType::Float;
        ScriptTargetKind targetKind = parseTransformTarget(target);
        std::string targetName;

        if (targetKind == ScriptTargetKind::None) {
            const ScriptFieldDefinition* field = findFieldDefinition(behavior, target);
            if (!field) {
                outDiagnostic = ScriptDiagnostic{
                    .path = path,
                    .line = pending.line,
                    .column = pending.column,
                    .reason = std::format("Unknown assignment target '{}'.", target),
                };
                return false;
            }

            targetKind = ScriptTargetKind::Field;
            targetName = field->name;
            valueType = field->type;
        }

        const std::optional<ScriptAssignOp> assignOp = parseAssignOp(op);
        if (!assignOp.has_value()) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = pending.column,
                .reason = std::format("Unsupported assignment operator '{}'.", op),
            };
            return false;
        }

        if (!isNumericType(valueType) && *assignOp != ScriptAssignOp::Set) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = pending.column,
                .reason = std::format("Only '=' is supported for {} assignments.", fieldTypeName(valueType)),
            };
            return false;
        }

        size_t errorColumn = 0;
        std::string errorReason;
        bool valid = false;
        switch (valueType) {
            case ScriptFieldType::Int:
            case ScriptFieldType::Float:
                valid = validateNumericExpression(expression,
                                                 behavior,
                                                 pending.eventName,
                                                 expressionColumn,
                                                 errorColumn,
                                                 errorReason);
                break;
            case ScriptFieldType::String:
                valid = validateStringExpression(expression,
                                                behavior,
                                                pending.eventName,
                                                errorColumn,
                                                errorReason);
                break;
            case ScriptFieldType::Bool:
                valid = validateBoolExpression(expression,
                                              behavior,
                                              errorColumn,
                                              errorReason);
                break;
            case ScriptFieldType::Vec3:
                valid = validateVec3Expression(expression,
                                              behavior,
                                              errorColumn,
                                              errorReason);
                break;
            default:
                break;
        }

        if (!valid) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = errorColumn > 0 ? errorColumn : expressionColumn,
                .reason = errorReason.empty() ? "Invalid assignment expression." : errorReason,
            };
            return false;
        }

        outStatement.kind = ScriptStatementKind::Assign;
        outStatement.assignOp = *assignOp;
        outStatement.targetKind = targetKind;
        outStatement.valueType = valueType;
        outStatement.line = pending.line;
        outStatement.column = expressionColumn;
        outStatement.targetName = std::move(targetName);
        outStatement.expression = expression;
        return true;
    }

    outDiagnostic = ScriptDiagnostic{
        .path = path,
        .line = pending.line,
        .column = pending.column,
        .reason = "Unsupported statement. Supported statements are print(...) and assignments such as transform.rotation.y += rotationSpeed * deltaTime.",
    };
    return false;
}

bool compilePendingStatement(const PendingStatement& pending,
                             const ScriptBehaviorDefinition& behavior,
                             const std::filesystem::path& path,
                             ScriptCompiledStatement& outStatement,
                             ScriptDiagnostic& outDiagnostic)
{
    if (!pending.isIf)
        return compileStatement(pending, behavior, path, outStatement, outDiagnostic);

    size_t errorColumn = 0;
    std::string errorReason;
    if (!validateBoolExpression(pending.text, behavior, errorColumn, errorReason)) {
        size_t numericColumn = 0;
        std::string numericReason;
        if (!validateNumericExpression(pending.text,
                                       behavior,
                                       pending.eventName,
                                       pending.column,
                                       numericColumn,
                                       numericReason)) {
            outDiagnostic = ScriptDiagnostic{
                .path = path,
                .line = pending.line,
                .column = numericColumn > 0 ? numericColumn : (errorColumn > 0 ? errorColumn : pending.column),
                .reason = !numericReason.empty()
                    ? numericReason
                    : (errorReason.empty() ? "Invalid if condition." : errorReason),
            };
            return false;
        }
    }

    outStatement.kind = ScriptStatementKind::IfStmt;
    outStatement.line = pending.line;
    outStatement.column = pending.column;
    outStatement.expression = pending.text;

    for (const PendingStatement& child : pending.thenBlock) {
        ScriptCompiledStatement compiled;
        if (!compilePendingStatement(child, behavior, path, compiled, outDiagnostic))
            return false;
        outStatement.thenBlock.push_back(std::move(compiled));
    }

    for (const PendingStatement& child : pending.elseBlock) {
        ScriptCompiledStatement compiled;
        if (!compilePendingStatement(child, behavior, path, compiled, outDiagnostic))
            return false;
        outStatement.elseBlock.push_back(std::move(compiled));
    }

    return true;
}

} // namespace

const ScriptEventBlock* selectEventBlock(const ScriptBehaviorDefinition& behavior, std::string_view eventName)
{
    if (eventName == "on_spawn")
        return &behavior.onSpawn;
    if (eventName == "on_update")
        return &behavior.onTick;
    if (eventName == "on_trigger")
        return &behavior.onTrigger;
    if (eventName == "on_signal")
        return &behavior.onSignal;
    return nullptr;
}

void logCompileDiagnostic(const ScriptDiagnostic& diagnostic)
{
    if (diagnostic.reason.starts_with("ERR001") ||
        diagnostic.reason.starts_with("ERR002")) {
        DEMON_LOG_ERROR("[DemonScript] Compile Error: {}", diagnostic.reason);
        return;
    }

    const std::string fileName = diagnostic.path.empty()
        ? std::string("<unknown>")
        : diagnostic.path.filename().string();

    if (diagnostic.line > 0 && diagnostic.column > 0) {
        DEMON_LOG_ERROR("DemonScript: {}: line {}, col {}: {}",
                        fileName,
                        diagnostic.line,
                        diagnostic.column,
                        diagnostic.reason);
    } else {
        DEMON_LOG_ERROR("DemonScript: {}: {}",
                        fileName,
                        diagnostic.reason);
    }
}

std::string formatRuntimeStartDiagnostic(const ScriptDiagnostic& diagnostic)
{
    const std::string fileName = diagnostic.path.empty()
        ? std::string("<unknown>")
        : diagnostic.path.filename().string();

    if (diagnostic.line > 0 && diagnostic.column > 0) {
        return std::format("[RUNTIME]: Error! Failed to Start the Game because in the Script ({}: line {}: col {}; {}).",
                           fileName,
                           diagnostic.line,
                           diagnostic.column,
                           diagnostic.reason);
    }

    return std::format("[RUNTIME]: Error! Failed to Start the Game because in the Script ({}; {}).",
                       fileName,
                       diagnostic.reason);
}

bool parseScriptFile(const std::filesystem::path& path,
                     ScriptBehaviorDefinition& outBehavior,
                     std::vector<ScriptDiagnostic>& outDiagnostics)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        addDiagnostic(outDiagnostics, path, 1, 1, "Failed to open script file.");
        return false;
    }

    std::vector<std::string> rawLines;
    std::string line;
    while (std::getline(in, line))
        rawLines.push_back(line);

    ScriptBehaviorDefinition behavior;
    behavior.path = path;
    std::error_code ec;
    behavior.lastWriteTime = std::filesystem::last_write_time(path, ec);

    static const std::regex behaviorHeaderRegex(R"(^\s*behavior\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{\s*$)");
    static const std::regex propRegex(R"(^\s*(prop|hidden)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?)\s*;?\s*$)");
    static const std::regex inputFieldRegex(R"(^\s*spawn\s+(InputField(?:[0-9]+)?(?:3D|IMG|UI)?)\s*;?\s*$)");
    static const std::regex eventHeaderRegex(R"(^\s*(on_[A-Za-z_][A-Za-z0-9_]*)\s*(\((.*?)\))?\s*\{\s*$)");
    static const std::regex ifBlockRegex(R"(^if\s*\(\s*(.+?)\s*\)\s*\{\s*$)");
    static const std::regex elseBlockRegex(R"(^else\s*\{\s*$)");
    static const std::regex closeElseBlockRegex(R"(^\}\s*else\s*\{\s*$)");

    enum class ParseState : uint8_t {
        ExpectBehavior = 0,
        InBehavior,
        InEvent,
        AfterBehavior,
    };

    ParseState state = ParseState::ExpectBehavior;
    std::string currentEvent;
    size_t currentEventStartLine = 0;
    std::unordered_set<std::string> declaredFields;
    std::unordered_set<std::string> declaredEvents;
    std::vector<PendingStatement> pendingStatements;

    auto trimmedLine = [&](size_t index) {
        const std::string& raw = rawLines[index];
        return trimCopy(raw.substr(0, raw.find("//")));
    };

    auto appendEventBody = [&](const std::string& statement) {
        if (std::string* body = selectEventBody(behavior, currentEvent)) {
            if (!body->empty())
                body->push_back('\n');
            body->append(statement);
        }
    };

    enum class PendingBlockClose : uint8_t {
        None,
        End,
        Else,
    };

    std::function<bool(size_t&, std::vector<PendingStatement>&, PendingBlockClose&)> parsePendingBlock;
    std::function<bool(size_t&, PendingStatement&)> parsePendingIf;

    parsePendingIf = [&](size_t& index, PendingStatement& outStatement) -> bool {
        const std::string trimmed = trimmedLine(index);
        std::smatch ifMatch;
        if (!std::regex_match(trimmed, ifMatch, ifBlockRegex)) {
            addDiagnostic(outDiagnostics,
                          path,
                          index + 1,
                          firstNonWhitespaceColumn(rawLines[index]),
                          "Invalid if statement. Expected 'if (condition) {'.");
            return false;
        }

        appendEventBody(trimmed);
        outStatement = PendingStatement{
            .eventName = currentEvent,
            .text = trimCopy(ifMatch[1].str()),
            .line = index + 1,
            .column = firstNonWhitespaceColumn(rawLines[index]),
            .isIf = true,
        };

        size_t cursor = index + 1;
        PendingBlockClose closeKind = PendingBlockClose::None;
        if (!parsePendingBlock(cursor, outStatement.thenBlock, closeKind))
            return false;

        if (closeKind == PendingBlockClose::None) {
            addDiagnostic(outDiagnostics,
                          path,
                          outStatement.line,
                          outStatement.column,
                          "If block is missing a closing '}'.");
            index = cursor;
            return false;
        }

        if (closeKind == PendingBlockClose::End) {
            size_t lookahead = cursor + 1;
            while (lookahead < rawLines.size() && trimmedLine(lookahead).empty())
                ++lookahead;

            if (lookahead < rawLines.size()) {
                const std::string elseTrimmed = trimmedLine(lookahead);
                if (std::regex_match(elseTrimmed, elseBlockRegex)) {
                    appendEventBody(elseTrimmed);
                    cursor = lookahead + 1;
                    PendingBlockClose elseClose = PendingBlockClose::None;
                    if (!parsePendingBlock(cursor, outStatement.elseBlock, elseClose))
                        return false;
                    if (elseClose != PendingBlockClose::End) {
                        addDiagnostic(outDiagnostics,
                                      path,
                                      lookahead + 1,
                                      firstNonWhitespaceColumn(rawLines[lookahead]),
                                      "Else block is missing a closing '}'.");
                        index = cursor;
                        return false;
                    }
                }
            }
        } else if (closeKind == PendingBlockClose::Else) {
            ++cursor;
            PendingBlockClose elseClose = PendingBlockClose::None;
            if (!parsePendingBlock(cursor, outStatement.elseBlock, elseClose))
                return false;
            if (elseClose != PendingBlockClose::End) {
                addDiagnostic(outDiagnostics,
                              path,
                              outStatement.line,
                              outStatement.column,
                              "Else block is missing a closing '}'.");
                index = cursor;
                return false;
            }
        }

        index = cursor;
        return true;
    };

    parsePendingBlock = [&](size_t& cursor,
                            std::vector<PendingStatement>& outStatements,
                            PendingBlockClose& closeKind) -> bool {
        closeKind = PendingBlockClose::None;

        for (; cursor < rawLines.size(); ++cursor) {
            const std::string trimmed = trimmedLine(cursor);
            if (trimmed.empty())
                continue;

            if (std::regex_match(trimmed, closeElseBlockRegex)) {
                appendEventBody(trimmed);
                closeKind = PendingBlockClose::Else;
                return true;
            }

            if (trimmed == "}") {
                appendEventBody(trimmed);
                closeKind = PendingBlockClose::End;
                return true;
            }

            if (std::regex_match(trimmed, elseBlockRegex)) {
                appendEventBody(trimmed);
                closeKind = PendingBlockClose::Else;
                return true;
            }

            if (std::regex_match(trimmed, ifBlockRegex)) {
                PendingStatement ifStatement;
                if (!parsePendingIf(cursor, ifStatement))
                    return false;
                outStatements.push_back(std::move(ifStatement));
                continue;
            }

            if (startsWithKeyword(trimmed, "if")) {
                addDiagnostic(outDiagnostics,
                              path,
                              cursor + 1,
                              firstNonWhitespaceColumn(rawLines[cursor]),
                              "Invalid if statement. Expected 'if (condition) {'.");
                return false;
            }

            outStatements.push_back(PendingStatement{
                .eventName = currentEvent,
                .text = trimmed,
                .line = cursor + 1,
                .column = firstNonWhitespaceColumn(rawLines[cursor]),
            });
            appendEventBody(trimmed);
        }

        return true;
    };

    for (size_t index = 0; index < rawLines.size(); ++index) {
        const size_t lineNumber = index + 1;
        const std::string& raw = rawLines[index];
        const std::string trimmed = trimCopy(raw.substr(0, raw.find("//")));
        if (trimmed.empty())
            continue;

        const size_t lineColumn = firstNonWhitespaceColumn(raw);
        std::smatch match;

        if (state == ParseState::ExpectBehavior) {
            if (!std::regex_match(trimmed, match, behaviorHeaderRegex)) {
                addDiagnostic(outDiagnostics,
                              path,
                              lineNumber,
                              lineColumn,
                              "Expected 'behavior <Name> {' at the top of the script.");
                continue;
            }

            behavior.name = match[1].str();
            state = ParseState::InBehavior;
            continue;
        }

        if (state == ParseState::AfterBehavior) {
            addDiagnostic(outDiagnostics,
                          path,
                          lineNumber,
                          lineColumn,
                          "Unexpected tokens found after the end of the behavior block.");
            continue;
        }

        if (state == ParseState::InBehavior) {
            if (trimmed == "}") {
                state = ParseState::AfterBehavior;
                continue;
            }

            if (std::regex_match(trimmed, match, propRegex)) {
                const bool hidden = match[1].str() == "hidden";
                const std::string fieldName = match[2].str();
                const ScriptFieldType type = parseFieldTypeToken(match[3].str());

                if (type == ScriptFieldType::None) {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Unsupported field type '{}'.", match[3].str()));
                    continue;
                }

                if (!declaredFields.insert(fieldName).second) {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Duplicate property '{}'.", fieldName));
                    continue;
                }

                auto value = [&]() -> std::optional<ScriptFieldValue> {
                    ScriptFieldValue fieldValue = makeDefaultFieldValue(fieldName, type, hidden);
                    const std::string text = trimCopy(match[4].str());
                    try {
                        switch (type) {
                            case ScriptFieldType::Bool:
                                if (text == "true") fieldValue.boolValue = true;
                                else if (text == "false") fieldValue.boolValue = false;
                                else return std::nullopt;
                                break;
                            case ScriptFieldType::Int:
                                fieldValue.intValue = std::stoll(text);
                                break;
                            case ScriptFieldType::Float:
                                fieldValue.floatValue = std::stof(text);
                                break;
                            case ScriptFieldType::String:
                                if (!parseStringLiteral(text, fieldValue.stringValue))
                                    return std::nullopt;
                                break;
                            case ScriptFieldType::Vec3:
                                if (!parseVec3Literal(text, fieldValue.vec3Value))
                                    return std::nullopt;
                                break;
                            case ScriptFieldType::Entity:
                            case ScriptFieldType::Entity3D:
                            case ScriptFieldType::EntityImage:
                            case ScriptFieldType::EntityUI:
                                fieldValue.entityValue = static_cast<uint64_t>(std::stoull(text));
                                break;
                            default:
                                return std::nullopt;
                        }
                    } catch (...) {
                        return std::nullopt;
                    }
                    return fieldValue;
                }();

                if (!value.has_value()) {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Invalid default value for '{}'.", fieldName));
                    continue;
                }

                behavior.fields.push_back(ScriptFieldDefinition{
                    .name = fieldName,
                    .type = type,
                    .hidden = hidden,
                    .defaultValue = *value,
                });
                continue;
            }

            if (std::regex_match(trimmed, match, inputFieldRegex)) {
                const std::string fieldName = match[1].str();
                const ScriptFieldType type = inputFieldTypeFromToken(fieldName);
                if (type == ScriptFieldType::None) {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Unsupported input field '{}'.", fieldName));
                    continue;
                }
                if (!declaredFields.insert(fieldName).second) {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Duplicate input field '{}'.", fieldName));
                    continue;
                }

                ScriptFieldValue value = makeDefaultFieldValue(fieldName, type, false);
                behavior.fields.push_back(ScriptFieldDefinition{
                    .name = fieldName,
                    .type = type,
                    .hidden = false,
                    .defaultValue = value,
                });
                continue;
            }

            if (startsWithKeyword(trimmed, "prop") || startsWithKeyword(trimmed, "hidden")) {
                addDiagnostic(outDiagnostics,
                              path,
                              lineNumber,
                              lineColumn,
                              "Invalid property declaration. Expected 'prop name: type = value'.");
                continue;
            }

            if (startsWithKeyword(trimmed, "spawn")) {
                addDiagnostic(outDiagnostics,
                              path,
                              lineNumber,
                              lineColumn,
                              "Invalid input field declaration. Expected 'spawn InputField;', 'spawn InputField3D;', 'spawn InputFieldIMG;', or 'spawn InputFieldUI;'.");
                continue;
            }

            if (std::regex_match(trimmed, match, eventHeaderRegex)) {
                const std::string canonicalEvent = normalizeEventName(match[1].str());
                if (canonicalEvent != "on_spawn" &&
                    canonicalEvent != "on_update" &&
                    canonicalEvent != "on_trigger" &&
                    canonicalEvent != "on_signal") {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Unsupported event '{}'.", match[1].str()));
                    continue;
                }

                if (!declaredEvents.insert(canonicalEvent).second) {
                    addDiagnostic(outDiagnostics,
                                  path,
                                  lineNumber,
                                  lineColumn,
                                  std::format("Duplicate event block '{}'.", canonicalEvent));
                    continue;
                }

                markEventPresent(behavior, canonicalEvent);
                currentEvent = canonicalEvent;
                currentEventStartLine = lineNumber;
                state = ParseState::InEvent;
                continue;
            }

            if (startsWithKeyword(trimmed, "behavior")) {
                addDiagnostic(outDiagnostics,
                              path,
                              lineNumber,
                              lineColumn,
                              "Only one behavior may be declared in a single .demonscript file.");
                continue;
            }

            addDiagnostic(outDiagnostics,
                          path,
                          lineNumber,
                          lineColumn,
                          "Unexpected statement at behavior scope. Only properties and event blocks are allowed here.");
            continue;
        }

        if (trimmed == "}") {
            currentEvent.clear();
            currentEventStartLine = 0;
            state = ParseState::InBehavior;
            continue;
        }

        if (std::regex_match(trimmed, ifBlockRegex)) {
            PendingStatement ifStatement;
            if (parsePendingIf(index, ifStatement))
                pendingStatements.push_back(std::move(ifStatement));
            continue;
        }

        if (startsWithKeyword(trimmed, "if")) {
            addDiagnostic(outDiagnostics,
                          path,
                          lineNumber,
                          lineColumn,
                          "Invalid if statement. Expected 'if (condition) {'.");
            continue;
        }

        if (std::regex_match(trimmed, elseBlockRegex) || std::regex_match(trimmed, closeElseBlockRegex)) {
            addDiagnostic(outDiagnostics,
                          path,
                          lineNumber,
                          lineColumn,
                          "Unexpected else block without a matching if.");
            continue;
        }

        pendingStatements.push_back(PendingStatement{
            .eventName = currentEvent,
            .text = trimmed,
            .line = lineNumber,
            .column = firstNonWhitespaceColumn(raw),
        });
        appendEventBody(trimmed);
    }

    if (state == ParseState::ExpectBehavior) {
        addDiagnostic(outDiagnostics, path, 1, 1, "Script is missing a valid behavior declaration.");
    } else if (state == ParseState::InEvent) {
        addDiagnostic(outDiagnostics,
                      path,
                      currentEventStartLine,
                      1,
                      std::format("Event block '{}' is missing a closing '}}'.", currentEvent));
    } else if (state == ParseState::InBehavior) {
        addDiagnostic(outDiagnostics,
                      path,
                      rawLines.empty() ? 1 : rawLines.size(),
                      1,
                      "Behavior block is missing a closing '}'.");
    }

    if (!outDiagnostics.empty())
        return false;

    for (const PendingStatement& pending : pendingStatements) {
        ScriptCompiledStatement compiled;
        ScriptDiagnostic diagnostic;
        if (!compilePendingStatement(pending, behavior, path, compiled, diagnostic)) {
            outDiagnostics.push_back(std::move(diagnostic));
            continue;
        }

        if (ScriptEventBlock* eventBlock = const_cast<ScriptEventBlock*>(selectEventBlock(behavior, pending.eventName)))
            eventBlock->statements.push_back(std::move(compiled));
    }

    if (!outDiagnostics.empty())
        return false;

    outBehavior = std::move(behavior);
    return true;
}

void executeEvent(Scene& scene,
                  ScriptComponent& script,
                  const ScriptBehaviorDefinition& behavior,
                  EntityID entityId,
                  std::string_view eventName,
                  float deltaTime,
                  std::string_view payload,
                  const ScriptEventBlock& block,
                  const RuntimeErrorReporter& reporter)
{
    ScriptExecutionContext ctx{
        .scene = scene,
        .script = script,
        .behavior = behavior,
        .entityId = entityId,
        .deltaTime = deltaTime,
        .eventName = eventName,
        .payload = payload,
    };

    for (const ScriptCompiledStatement& statement : block.statements) {
        std::string reason;
        if (executeStatement(statement, ctx, reason))
            continue;
        if (reporter)
            reporter(statement, reason);
    }
}

} // namespace Demon::ScriptDetail
