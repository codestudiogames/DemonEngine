#pragma once
// ==============================================================================
//  DemonEngine::UUID  —  64-bit pseudo-random unique identifier
// ==============================================================================
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <format>

namespace Demon {

class UUID {
public:
    UUID()               : m_id(generate()) {}
    explicit UUID(uint64_t id) : m_id(id)   {}

    operator uint64_t()  const noexcept { return m_id; }
    bool operator==(const UUID&) const = default;

    [[nodiscard]] std::string toString() const { return std::format("{:016X}", m_id); }

private:
    static uint64_t generate() {
        static std::mt19937_64 rng{std::random_device{}()};
        static std::uniform_int_distribution<uint64_t> dist;
        return dist(rng);
    }
    uint64_t m_id;
};

} // namespace Demon

template<>
struct std::hash<Demon::UUID> {
    size_t operator()(const Demon::UUID& id) const noexcept {
        return std::hash<uint64_t>{}(static_cast<uint64_t>(id));
    }
};
