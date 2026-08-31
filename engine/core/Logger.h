#pragma once
// ==============================================================================
//  DemonEngine::Logger
//  Lightweight, thread-safe, colour console logger using C++20 formatting.
// ==============================================================================

#include <format>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <atomic>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace Demon {

enum class LogLevel : uint8_t { Trace=0, Info, Warn, Error, Fatal };

struct LogMessage {
    LogLevel    level = LogLevel::Info;
    std::string message;
    std::string file;
    uint32_t    line = 0;
};

class Logger {
public:
    using Sink = std::function<void(const LogMessage&)>;

    static void init(LogLevel minLevel = LogLevel::Trace);
    static void setLevel(LogLevel l) { s_minLevel = l; }
    [[nodiscard]] static uint64_t addSink(Sink sink);
    static void removeSink(uint64_t sinkId);

    template<typename... Args>
    static void log(LogLevel level,
                    const std::source_location& loc,
                    std::string_view fmt,
                    Args&&... args)
    {
        if (level < s_minLevel) return;
        std::string msg = std::vformat(fmt, std::make_format_args(args...));
        printFormatted(level, loc, msg);
    }

    [[noreturn]] static void assertFail(const std::source_location& loc,
                                        std::string_view msg)
    {
        printFormatted(LogLevel::Fatal, loc,
                       std::string("Assertion failed: ").append(msg));
#if defined(_MSC_VER)
        __debugbreak();
        std::abort();
#elif defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#else
        std::abort();
#endif
    }

private:
    static void printFormatted(LogLevel level,
                               const std::source_location& loc,
                               std::string_view msg);
    static void dispatchToSinks(const LogMessage& message);

    inline static LogLevel   s_minLevel { LogLevel::Trace };
    inline static std::mutex s_mutex;
    inline static std::mutex s_sinkMutex;
    inline static std::unordered_map<uint64_t, Sink> s_sinks;
    inline static std::atomic_uint64_t s_nextSinkId {1};
};

} // namespace Demon


// ── Logging macros ────────────────────────────────────────────────────────────

#define DEMON_LOG_TRACE(...) Demon::Logger::log(Demon::LogLevel::Trace, std::source_location::current(), __VA_ARGS__)
#define DEMON_LOG_INFO(...)  Demon::Logger::log(Demon::LogLevel::Info,  std::source_location::current(), __VA_ARGS__)
#define DEMON_LOG_WARN(...)  Demon::Logger::log(Demon::LogLevel::Warn,  std::source_location::current(), __VA_ARGS__)
#define DEMON_LOG_ERROR(...) Demon::Logger::log(Demon::LogLevel::Error, std::source_location::current(), __VA_ARGS__)
#define DEMON_LOG_FATAL(...) Demon::Logger::log(Demon::LogLevel::Fatal, std::source_location::current(), __VA_ARGS__)


// ── Assertion macro — NO backslash continuations, so no end-of-file warning ──

#define DEMON_ASSERT(cond, ...) do { if (!(cond)) Demon::Logger::assertFail(std::source_location::current(), __VA_ARGS__); } while(false)
