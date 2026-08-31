#pragma once
// ==============================================================================
//  DemonEngine::ThreadingManager
//  Lightweight worker thread control + debug utilities.
// ==============================================================================
#include "DemonPCH.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>

namespace Demon {

class ThreadingManager {
public:
    using Task = std::function<void()>;

    ThreadingManager();
    ~ThreadingManager();

    ThreadingManager(const ThreadingManager&)            = delete;
    ThreadingManager& operator=(const ThreadingManager&) = delete;

    void init(uint32_t workerCount,
              uint64_t affinityMask = 0,
              std::wstring_view namePrefix = L"DemonWorker");
    void shutdown();

    void submit(Task task);
    void waitIdle();

    void updateDebug(uint32_t stallWarningMs);

    void setMainThread();
    [[nodiscard]] bool     isMainThread() const { return m_mainThreadId == std::this_thread::get_id(); }
    [[nodiscard]] uint32_t workerCount()  const { return static_cast<uint32_t>(m_workers.size()); }

    static void setCurrentThreadName(std::wstring_view name);
    static void setCurrentThreadAffinity(uint64_t mask);

private:
    struct Worker {
        std::thread               thread;
        std::atomic<uint64_t>     lastHeartbeat{0};
        std::atomic<uint64_t>     lastWarn{0};
    };

    void workerLoop(Worker* worker, uint32_t index, std::wstring name);
    static uint64_t nowMs();

    std::vector<std::unique_ptr<Worker>> m_workers;
    std::deque<Task>                     m_tasks;
    std::mutex                           m_mutex;
    std::condition_variable              m_cv;
    std::condition_variable              m_idleCv;
    std::atomic<bool>                    m_running{false};
    std::atomic<uint32_t>                m_inFlight{0};
    std::thread::id                      m_mainThreadId{};
    uint64_t                             m_affinityMask = 0;
    std::wstring                         m_namePrefix   = L"DemonWorker";
};

} // namespace Demon
