#pragma once

#include "core/Logger.h"

namespace Demon {

class ConsolePanel {
public:
    ConsolePanel();
    ~ConsolePanel();
    void render();
    void clear();

private:
    struct LogEntry {
        std::string message;
        std::string file;
        int line = 0;
        int level = 1;
        int count = 1;
    };

    void addEntry(LogEntry entry);

    inline static ConsolePanel* s_instance = nullptr;
    std::mutex m_entriesMutex;
    std::vector<LogEntry> m_entries;
    uint64_t m_loggerSinkId = 0;
    bool m_scrollToBottom = false;
    bool m_collapseIdentical = true;
    bool m_showEngineInfo = true;
    bool m_filterTrace = true;
    bool m_filterInfo = true;
    bool m_filterWarn = true;
    bool m_filterError = true;
    char m_filterText[160]{};

    std::mutex m_mutex;
    std::vector<LogMessage> m_messages;
    uint64_t m_sinkId = 0;
    bool m_autoScroll = true;
    bool m_showTrace = true;
    bool m_showInfo = true;
    bool m_showWarnings = true;
    bool m_showErrors = true;
    char m_filter[160]{};
};

} // namespace Demon
