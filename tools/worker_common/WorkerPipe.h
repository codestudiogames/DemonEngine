#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#   include <shellapi.h>
#   include <shobjidl.h>
#endif

namespace Demon::WorkerIPC {

using CommandHandler = std::function<std::string(std::string_view)>;

inline std::wstring widenAscii(std::string_view text)
{
    return std::wstring(text.begin(), text.end());
}

inline std::string trimCommand(std::string text)
{
    if (const auto nullPos = text.find('\0'); nullPos != std::string::npos)
        text.resize(nullPos);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    return text;
}

inline std::wstring pipeNameForProcess(std::string_view processName)
{
    return L"\\\\.\\pipe\\Demon_" + widenAscii(processName);
}

inline std::string currentProcessIdText()
{
#ifdef _WIN32
    return std::to_string(GetCurrentProcessId());
#else
    return "0";
#endif
}

inline int runNamedPipeWorker(const std::string& processName,
                              CommandHandler customHandler = {})
{
#ifndef _WIN32
    (void)processName;
    (void)customHandler;
    return 1;
#else
    SetCurrentProcessExplicitAppUserModelID(L"CodeStudioGames.DemonEngine");

    const std::wstring pipeName = pipeNameForProcess(processName);
    std::atomic_bool running = true;

    while (running.load()) {
        HANDLE pipe = CreateNamedPipeW(pipeName.c_str(),
                                       PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                       PIPE_UNLIMITED_INSTANCES,
                                       4096,
                                       4096,
                                       0,
                                       nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return 2;

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) {
            CloseHandle(pipe);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::vector<char> buffer(4096);
        DWORD bytesRead = 0;
        std::string response = "UNKNOWN\n";
        if (ReadFile(pipe,
                     buffer.data(),
                     static_cast<DWORD>(buffer.size() - 1),
                     &bytesRead,
                     nullptr))
        {
            buffer[bytesRead] = '\0';
            const std::string command = trimCommand(std::string(buffer.data(), bytesRead));

            if (command == "PING") {
                response = "PONG " + processName + " " + currentProcessIdText() + "\n";
            } else if (command == "STATUS") {
                response = "STATUS " + processName + " IDLE\n";
            } else if (command == "SHUTDOWN") {
                response = "BYE " + processName + "\n";
                running.store(false);
            } else if (customHandler) {
                response = customHandler(command);
                if (response.empty())
                    response = "OK " + processName + "\n";
                if (response.back() != '\n')
                    response.push_back('\n');
            }
        }

        DWORD bytesWritten = 0;
        (void)WriteFile(pipe,
                        response.data(),
                        static_cast<DWORD>(response.size()),
                        &bytesWritten,
                        nullptr);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    return 0;
#endif
}

} // namespace Demon::WorkerIPC
