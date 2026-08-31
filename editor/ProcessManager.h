#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Demon {

enum class DemonProcessState {
    NotStarted,
    Running,
    Exited,
    MissingExecutable,
    FailedToStart
};

struct DemonManagedProcess {
    std::wstring displayName;
    std::wstring executableName;
    std::wstring pipeName;
    DWORD pid = 0;
    bool critical = false;
    DemonProcessState state = DemonProcessState::NotStarted;
};

class DemonProcessManager {
public:
    DemonProcessManager() = default;
    ~DemonProcessManager();

    DemonProcessManager(const DemonProcessManager&) = delete;
    DemonProcessManager& operator=(const DemonProcessManager&) = delete;

    bool startPersistentProcesses(const std::filesystem::path& executableDirectory,
                                  const std::filesystem::path& logDirectory);
    void shutdown();
    void updateHealth();

    [[nodiscard]] bool isStarted() const { return m_started; }
    [[nodiscard]] std::wstring statusLine() const;
    [[nodiscard]] const std::vector<DemonManagedProcess>& children() const { return m_children; }

private:
    bool createJobObject();
    bool spawnChild(DemonManagedProcess& process, const std::wstring& extraArguments);
    bool spawnCrashGuard(const std::filesystem::path& logDirectory);
    void closeProcessInformation(PROCESS_INFORMATION& processInfo);
    static bool sendPipeCommand(const std::wstring& pipeName,
                                std::string_view command,
                                std::string* response = nullptr);

    std::filesystem::path m_executableDirectory;
    std::vector<DemonManagedProcess> m_children;
    HANDLE m_jobObject = nullptr;
    HANDLE m_crashGuardStopEvent = nullptr;
    std::wstring m_crashGuardStopEventName;
    PROCESS_INFORMATION m_crashGuardProcess{};
    bool m_started = false;
};

} // namespace Demon
