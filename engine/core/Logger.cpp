// ==============================================================================
//  DemonEngine::Logger  --  Full Implementation
// ==============================================================================
#include "Logger.h"

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#endif

namespace Demon {

static constexpr std::string_view levelTag(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?????";
}

static constexpr const char* levelColour(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "\033[90m";
        case LogLevel::Info:  return "\033[32m";
        case LogLevel::Warn:  return "\033[33m";
        case LogLevel::Error: return "\033[31m";
        case LogLevel::Fatal: return "\033[35m";
    }
    return "\033[0m";
}

void Logger::init(LogLevel minLevel)
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode))
            SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    s_minLevel = minLevel;
    DEMON_LOG_INFO("DemonEngine Logger v1.2 \u2014 ready.");
}

uint64_t Logger::addSink(Sink sink)
{
    if (!sink)
        return 0;

    const uint64_t sinkId = s_nextSinkId.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(s_sinkMutex);
    s_sinks.emplace(sinkId, std::move(sink));
    return sinkId;
}

void Logger::removeSink(uint64_t sinkId)
{
    if (sinkId == 0)
        return;

    std::lock_guard lock(s_sinkMutex);
    s_sinks.erase(sinkId);
}

void Logger::printFormatted(LogLevel level,
                             const std::source_location& loc,
                             std::string_view msg)
{
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    auto t   = system_clock::to_time_t(now);
    char tbuf[16];
#ifdef _WIN32
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);
#else
    std::tm tm{};
    localtime_r(&t, &tm);
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);
#endif
    std::string_view file = loc.file_name();
    if (auto p = file.find_last_of("/\\"); p != std::string_view::npos)
        file = file.substr(p + 1);

    LogMessage sinkMessage;
    sinkMessage.level = level;
    sinkMessage.message = std::string(msg);
    sinkMessage.file = std::string(file);
    sinkMessage.line = static_cast<uint32_t>(loc.line());
    dispatchToSinks(sinkMessage);

    std::lock_guard lock(s_mutex);
    std::cerr
        << levelColour(level)
        << std::format("[{}.{:03d}] [{}]  {}  \033[90m({}:{})\033[0m\n",
               tbuf, static_cast<int>(ms.count()),
               levelTag(level), msg,
               file, loc.line());
}

void Logger::dispatchToSinks(const LogMessage& message)
{
    std::vector<Sink> sinks;
    {
        std::lock_guard lock(s_sinkMutex);
        sinks.reserve(s_sinks.size());
        for (const auto& [id, sink] : s_sinks) {
            (void)id;
            sinks.push_back(sink);
        }
    }

    for (const auto& sink : sinks) {
        if (sink)
            sink(message);
    }
}

} // namespace Demon
