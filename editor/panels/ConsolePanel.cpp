#include <imgui.h>
#include "ConsolePanel.h"
#include "../EditorSettings.h"
#include "core/Logger.h"
namespace Demon {

namespace {

int toConsoleLevel(LogLevel level)
{
    switch (level) {
        case LogLevel::Trace: return 0;
        case LogLevel::Info:  return 1;
        case LogLevel::Warn:  return 2;
        case LogLevel::Error: return 3;
        case LogLevel::Fatal: return 4;
    }
    return 1;
}

bool isRuntimeConsoleMessage(const std::string& message)
{
    return message.find("[SCRIPT]") != std::string::npos ||
           message.find("[RUNTIME]") != std::string::npos ||
           message.find("DemonScript:") != std::string::npos ||
           message.find("Script error") != std::string::npos ||
           message.find("Video Memory") != std::string::npos ||
           message.find("VRAM") != std::string::npos;
}

} // namespace

ConsolePanel::ConsolePanel()
{
    s_instance = this;
    m_loggerSinkId = Logger::addSink([this](const LogMessage& message) {
        addEntry(LogEntry{
            .message = std::format("[{}] {}", message.level == LogLevel::Trace ? "TRACE" :
                                              message.level == LogLevel::Info  ? "INFO"  :
                                              message.level == LogLevel::Warn  ? "WARN"  :
                                              message.level == LogLevel::Error ? "ERROR" : "FATAL",
                                   message.message),
            .file = message.file,
            .line = static_cast<int>(message.line),
            .level = toConsoleLevel(message.level),
        });
    });
}

ConsolePanel::~ConsolePanel()
{
    Logger::removeSink(m_loggerSinkId);
    if (s_instance == this)
        s_instance = nullptr;
}

void ConsolePanel::render() {
    ImGui::Begin("Console", nullptr, editorPanelFlags());
    if (ImGui::SmallButton("Clear")) clear();
    ImGui::SameLine();
    ImGui::Checkbox("Collapse", &m_collapseIdentical);
    ImGui::SameLine();
    ImGui::Checkbox("Engine Info", &m_showEngineInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Trace", &m_filterTrace);
    ImGui::SameLine();
    ImGui::Checkbox("Info",  &m_filterInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warn",  &m_filterWarn);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &m_filterError);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##filter", "Filter...", m_filterText, sizeof(m_filterText));
    ImGui::Separator();

    ImGui::BeginChild("##scroll", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar);
    std::vector<LogEntry> entries;
    bool scrollToBottom = false;
    {
        std::scoped_lock lock(m_entriesMutex);
        entries = m_entries;
        scrollToBottom = m_scrollToBottom;
        m_scrollToBottom = false;
    }
    for (auto& e : entries) {
        if (e.level == 0 && !m_filterTrace) continue;
        if (e.level == 1 && !m_filterInfo)  continue;
        if (e.level == 2 && !m_filterWarn)  continue;
        if (e.level >= 3 && !m_filterError) continue;
        if (e.level <= 1 && !m_showEngineInfo && !isRuntimeConsoleMessage(e.message)) continue;
        if (m_filterText[0] && e.message.find(m_filterText) == std::string::npos) continue;

        ImVec4 col = {0.9f, 0.9f, 0.9f, 1.f};
        if (e.level == 0) col = {0.5f, 0.5f, 0.5f, 1.f};
        if (e.level == 2) col = {1.0f, 0.8f, 0.0f, 1.f};
        if (e.level == 3) col = {1.0f, 0.3f, 0.3f, 1.f};
        if (e.level == 4) col = {1.0f, 0.0f, 1.0f, 1.f};

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (e.count > 1)
            ImGui::Text("[%d] %s", e.count, e.message.c_str());
        else
            ImGui::TextUnformatted(e.message.c_str());
        ImGui::PopStyleColor();
    }
    if (scrollToBottom)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void ConsolePanel::clear()
{
    std::scoped_lock lock(m_entriesMutex);
    m_entries.clear();
}
void ConsolePanel::addEntry(LogEntry e) {
    std::scoped_lock lock(m_entriesMutex);
    if (m_collapseIdentical && !m_entries.empty() && m_entries.back().message == e.message)
        { m_entries.back().count++; return; }
    m_entries.push_back(std::move(e));
    m_scrollToBottom = true;
}

} // namespace Demon
