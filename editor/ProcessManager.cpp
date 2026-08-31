#include "ProcessManager.h"

#include "core/Logger.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <sstream>
#include <string_view>
#include <thread>

namespace Demon {
namespace {

std::wstring quoteArg(const std::wstring& text)
{
    std::wstring quoted = L"\"";
    for (wchar_t ch : text) {
        if (ch == L'"')
            quoted += L"\\\"";
        else
            quoted += ch;
    }
    quoted += L"\"";
    return quoted;
}

std::string narrowAscii(const std::wstring& text)
{
    std::string result;
    result.reserve(text.size());
    for (wchar_t ch : text)
        result.push_back(static_cast<char>(ch));
    return result;
}

std::wstring stateText(DemonProcessState state)
{
    switch (state) {
        case DemonProcessState::NotStarted:        return L"not-started";
        case DemonProcessState::Running:           return L"OK";
        case DemonProcessState::Exited:            return L"exited";
        case DemonProcessState::MissingExecutable: return L"missing";
        case DemonProcessState::FailedToStart:     return L"failed";
    }
    return L"unknown";
}

bool processIsRunning(HANDLE process)
{
    if (!process)
        return false;
    DWORD exitCode = 0;
    return GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
}

} // namespace

DemonProcessManager::~DemonProcessManager()
{
    shutdown();
}

bool DemonProcessManager::startPersistentProcesses(const std::filesystem::path& executableDirectory,
                                                   const std::filesystem::path& logDirectory)
{
    if (m_started)
        return true;

    m_executableDirectory = executableDirectory;

    if (!createJobObject())
        return false;

    const DWORD parentPid = GetCurrentProcessId();
    const std::wstring parentArg = L" --parent-pid " + std::to_wstring(parentPid);

    m_children = {
        {L"DemonAssetWorker",    L"DemonAssetWorker.exe",    L"\\\\.\\pipe\\Demon_DemonAssetWorker",    0, true},
        {L"DemonShaderCompiler", L"DemonShaderCompiler.exe", L"\\\\.\\pipe\\Demon_DemonShaderCompiler", 0, true},
        {L"DemonStreamHost",     L"DemonStreamHost.exe",     L"\\\\.\\pipe\\Demon_DemonStreamHost",     0, true},
        {L"DemonThumbGen",       L"DemonThumbGen.exe",       L"\\\\.\\pipe\\Demon_DemonThumbGen",       0, false},
        {L"DemonCacheServer",    L"DemonCacheServer.exe",    L"\\\\.\\pipe\\Demon_DemonCacheServer",    0, false},
        {L"DemonScriptVM",       L"DemonScriptVM.exe",       L"\\\\.\\pipe\\Demon_DemonScriptVM",       0, true},
        {L"DemonPhysicsServer",  L"DemonPhysicsServer.exe",  L"\\\\.\\pipe\\Demon_DemonPhysicsServer",  0, true},
    };

    for (DemonManagedProcess& child : m_children) {
        const std::wstring args =
            L" --worker --worker-name " + quoteArg(child.displayName) + parentArg;
        (void)spawnChild(child, args);
    }

    m_crashGuardStopEventName =
        L"Local\\DemonCrashGuard_Stop_" + std::to_wstring(parentPid) + L"_" +
        std::to_wstring(GetTickCount64());
    m_crashGuardStopEvent = CreateEventW(nullptr,
                                         TRUE,
                                         FALSE,
                                         m_crashGuardStopEventName.c_str());
    if (!m_crashGuardStopEvent)
        DEMON_LOG_WARN("DemonProcessManager: failed to create CrashGuard stop event.");

    (void)spawnCrashGuard(logDirectory);
    m_started = true;
    DEMON_LOG_INFO("DemonProcessManager: persistent process ecosystem started.");
    return true;
}

void DemonProcessManager::shutdown()
{
    if (!m_started && !m_jobObject && !m_crashGuardProcess.hProcess)
        return;

    DEMON_LOG_INFO("DemonProcessManager: shutting down child processes.");

    for (const DemonManagedProcess& child : m_children)
        (void)sendPipeCommand(child.pipeName, "SHUTDOWN\n");

    for (DemonManagedProcess& child : m_children) {
        if (child.pid == 0)
            continue;
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, child.pid);
        if (process) {
            WaitForSingleObject(process, 750);
            CloseHandle(process);
        }
    }

    if (m_crashGuardStopEvent)
        SetEvent(m_crashGuardStopEvent);

    if (m_crashGuardProcess.hProcess) {
        WaitForSingleObject(m_crashGuardProcess.hProcess, 2000);
        closeProcessInformation(m_crashGuardProcess);
    }

    if (m_crashGuardStopEvent) {
        CloseHandle(m_crashGuardStopEvent);
        m_crashGuardStopEvent = nullptr;
    }

    if (m_jobObject) {
        CloseHandle(m_jobObject);
        m_jobObject = nullptr;
    }

    m_children.clear();
    m_started = false;
}

void DemonProcessManager::updateHealth()
{
    for (DemonManagedProcess& child : m_children) {
        if (child.pid == 0)
            continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, child.pid);
        if (!process) {
            child.state = DemonProcessState::Exited;
            continue;
        }
        child.state = processIsRunning(process)
            ? DemonProcessState::Running
            : DemonProcessState::Exited;
        CloseHandle(process);
    }
}

std::wstring DemonProcessManager::statusLine() const
{
    std::wostringstream stream;
    stream << L"Processes: ";
    if (m_crashGuardProcess.dwProcessId != 0)
        stream << L"CrashGuard " << m_crashGuardProcess.dwProcessId << L" | ";

    for (size_t i = 0; i < m_children.size(); ++i) {
        const DemonManagedProcess& child = m_children[i];
        std::wstring shortName = child.displayName;
        if (shortName.starts_with(L"Demon"))
            shortName.erase(0, 5);
        stream << shortName << L":" << stateText(child.state);
        if (child.pid != 0 && child.state == DemonProcessState::Running)
            stream << L"(" << child.pid << L")";
        if (i + 1 < m_children.size())
            stream << L" ";
    }
    return stream.str();
}

bool DemonProcessManager::createJobObject()
{
    m_jobObject = CreateJobObjectW(nullptr, nullptr);
    if (!m_jobObject) {
        DEMON_LOG_ERROR("DemonProcessManager: CreateJobObject failed ({})", GetLastError());
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(m_jobObject,
                                 JobObjectExtendedLimitInformation,
                                 &limits,
                                 sizeof(limits)))
    {
        DEMON_LOG_ERROR("DemonProcessManager: SetInformationJobObject failed ({})", GetLastError());
        return false;
    }
    return true;
}

bool DemonProcessManager::spawnChild(DemonManagedProcess& process,
                                     const std::wstring& extraArguments)
{
    const std::filesystem::path executable = m_executableDirectory / process.executableName;
    if (!std::filesystem::exists(executable)) {
        process.state = DemonProcessState::MissingExecutable;
        DEMON_LOG_WARN("DemonProcessManager: missing {}", executable.string());
        return false;
    }

    std::wstring commandLine = quoteArg(executable.wstring()) + extraArguments;
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.wstring().c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        m_executableDirectory.wstring().c_str(),
                        &startup,
                        &info))
    {
        process.state = DemonProcessState::FailedToStart;
        DEMON_LOG_ERROR("DemonProcessManager: failed to start {} ({})",
                        executable.string(),
                        GetLastError());
        return false;
    }

    if (m_jobObject && !AssignProcessToJobObject(m_jobObject, info.hProcess)) {
        DEMON_LOG_WARN("DemonProcessManager: failed to assign {} to job object ({})",
                       executable.string(),
                       GetLastError());
    }

    process.pid = info.dwProcessId;
    process.state = DemonProcessState::Running;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    DEMON_LOG_INFO("DemonProcessManager: started {} pid={}",
                   executable.filename().string(),
                   process.pid);
    return true;
}

bool DemonProcessManager::spawnCrashGuard(const std::filesystem::path& logDirectory)
{
    const std::filesystem::path executable = m_executableDirectory / L"DemonCrashGuard.exe";
    if (!std::filesystem::exists(executable)) {
        DEMON_LOG_WARN("DemonProcessManager: DemonCrashGuard.exe missing at {}",
                       executable.string());
        return false;
    }

    std::wstring commandLine = quoteArg(executable.wstring());
    commandLine += L" --parent-pid " + std::to_wstring(GetCurrentProcessId());
    if (!m_crashGuardStopEventName.empty())
        commandLine += L" --stop-event " + quoteArg(m_crashGuardStopEventName);
    commandLine += L" --log-dir " + quoteArg(logDirectory.wstring());

    for (const DemonManagedProcess& child : m_children) {
        if (child.pid == 0)
            continue;
        std::wstring token = child.displayName + L"|" +
                             std::to_wstring(child.pid) + L"|" +
                             child.pipeName + L"|" +
                             (child.critical ? L"critical" : L"noncritical");
        commandLine += L" --monitor " + quoteArg(token);
    }

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable.wstring().c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        m_executableDirectory.wstring().c_str(),
                        &startup,
                        &m_crashGuardProcess))
    {
        DEMON_LOG_ERROR("DemonProcessManager: failed to start DemonCrashGuard ({})", GetLastError());
        return false;
    }

    CloseHandle(m_crashGuardProcess.hThread);
    m_crashGuardProcess.hThread = nullptr;
    DEMON_LOG_INFO("DemonProcessManager: started DemonCrashGuard pid={}",
                   m_crashGuardProcess.dwProcessId);
    return true;
}

void DemonProcessManager::closeProcessInformation(PROCESS_INFORMATION& processInfo)
{
    if (processInfo.hThread) {
        CloseHandle(processInfo.hThread);
        processInfo.hThread = nullptr;
    }
    if (processInfo.hProcess) {
        CloseHandle(processInfo.hProcess);
        processInfo.hProcess = nullptr;
    }
    processInfo.dwProcessId = 0;
    processInfo.dwThreadId = 0;
}

bool DemonProcessManager::sendPipeCommand(const std::wstring& pipeName,
                                          std::string_view command,
                                          std::string* response)
{
    if (pipeName.empty())
        return false;
    if (!WaitNamedPipeW(pipeName.c_str(), 100))
        return false;

    HANDLE pipe = CreateFileW(pipeName.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return false;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD written = 0;
    const BOOL wrote = WriteFile(pipe,
                                 command.data(),
                                 static_cast<DWORD>(command.size()),
                                 &written,
                                 nullptr);
    bool ok = wrote == TRUE;

    if (ok && response) {
        char buffer[512]{};
        DWORD read = 0;
        if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr)) {
            buffer[read] = '\0';
            *response = buffer;
        }
    }

    CloseHandle(pipe);
    return ok;
}

} // namespace Demon
