#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Monitor {
    std::string name;
    DWORD pid = 0;
    std::string pipeName;
    bool critical = false;
    uint32_t missedHeartbeats = 0;
    bool reported = false;
};

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::string current;
    for (char ch : text) {
        if (ch == separator) {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(std::move(current));
    return parts;
}

Monitor parseMonitor(const std::string& text)
{
    Monitor monitor;
    const auto parts = split(text, '|');
    if (parts.size() >= 1)
        monitor.name = parts[0];
    if (parts.size() >= 2)
        monitor.pid = static_cast<DWORD>(std::stoul(parts[1]));
    if (parts.size() >= 3)
        monitor.pipeName = parts[2];
    if (parts.size() >= 4)
        monitor.critical = parts[3] == "critical";
    return monitor;
}

bool processAlive(DWORD pid)
{
    if (pid == 0)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

bool pingPipe(const std::string& pipeName)
{
    if (pipeName.empty())
        return false;

    if (!WaitNamedPipeA(pipeName.c_str(), 100))
        return false;

    HANDLE pipe = CreateFileA(pipeName.c_str(),
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

    constexpr char request[] = "PING\n";
    DWORD written = 0;
    const BOOL wrote = WriteFile(pipe,
                                 request,
                                 static_cast<DWORD>(sizeof(request) - 1),
                                 &written,
                                 nullptr);

    char response[256]{};
    DWORD read = 0;
    const BOOL received = wrote && ReadFile(pipe, response, sizeof(response) - 1, &read, nullptr);
    CloseHandle(pipe);

    if (!received)
        return false;
    response[read] = '\0';
    return std::string_view(response).starts_with("PONG");
}

std::string timestamp()
{
    SYSTEMTIME local{};
    GetLocalTime(&local);
    char buffer[64]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04u-%02u-%02u_%02u-%02u-%02u",
                  local.wYear,
                  local.wMonth,
                  local.wDay,
                  local.wHour,
                  local.wMinute,
                  local.wSecond);
    return buffer;
}

class CrashLog {
public:
    explicit CrashLog(fs::path directory)
    {
        std::error_code ec;
        fs::create_directories(directory, ec);
        m_file.open(directory / ("crashguard_" + timestamp() + ".dlog"),
                    std::ios::out | std::ios::app);
    }

    void write(const std::string& text)
    {
        if (!m_file)
            return;
        m_file << "[" << timestamp() << "] " << text << '\n';
        m_file.flush();
    }

private:
    std::ofstream m_file;
};

} // namespace

int main(int argc, char** argv)
{
    SetCurrentProcessExplicitAppUserModelID(L"CodeStudioGames.DemonEngine");

    DWORD parentPid = 0;
    std::string stopEventName;
    fs::path logDirectory = "demon_logs";
    std::vector<Monitor> monitors;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--parent-pid" && i + 1 < argc) {
            parentPid = static_cast<DWORD>(std::stoul(argv[++i]));
        } else if (arg == "--stop-event" && i + 1 < argc) {
            stopEventName = argv[++i];
        } else if (arg == "--log-dir" && i + 1 < argc) {
            logDirectory = fs::path(argv[++i]);
        } else if (arg == "--monitor" && i + 1 < argc) {
            monitors.push_back(parseMonitor(argv[++i]));
        }
    }

    CrashLog log(logDirectory);
    log.write("DemonCrashGuard started. Parent PID=" + std::to_string(parentPid) +
              " monitors=" + std::to_string(monitors.size()));

    HANDLE parent = nullptr;
    if (parentPid != 0)
        parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentPid);

    HANDLE stopEvent = nullptr;
    if (!stopEventName.empty())
        stopEvent = OpenEventA(SYNCHRONIZE, FALSE, stopEventName.c_str());

    bool parentCrashReported = false;
    while (true) {
        if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
            log.write("Stop event received. CrashGuard exiting normally.");
            break;
        }

        if (parent && WaitForSingleObject(parent, 0) == WAIT_OBJECT_0) {
            if (!parentCrashReported) {
                log.write("DemonEditor parent process exited without CrashGuard stop event.");
                MessageBoxA(nullptr,
                            "DemonEditor exited unexpectedly. CrashGuard collected a process health log.",
                            "Demon Crash Guard",
                            MB_ICONWARNING | MB_OK);
                parentCrashReported = true;
            }
            break;
        }

        for (Monitor& monitor : monitors) {
            const bool alive = processAlive(monitor.pid);
            const bool heartbeat = alive && pingPipe(monitor.pipeName);

            if (heartbeat) {
                monitor.missedHeartbeats = 0;
                monitor.reported = false;
                continue;
            }

            ++monitor.missedHeartbeats;
            if (monitor.missedHeartbeats >= 3 && !monitor.reported) {
                std::ostringstream line;
                line << monitor.name << " missed " << monitor.missedHeartbeats
                     << " heartbeats. PID=" << monitor.pid
                     << " alive=" << (alive ? "yes" : "no")
                     << " critical=" << (monitor.critical ? "yes" : "no");
                log.write(line.str());
                monitor.reported = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (stopEvent)
        CloseHandle(stopEvent);
    if (parent)
        CloseHandle(parent);
    log.write("DemonCrashGuard stopped.");
    return 0;
}
