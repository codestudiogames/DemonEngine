// ==============================================================================
//  DemonEngine::ThreadingManager
// ==============================================================================
#include "ThreadingManager.h"
#include "Logger.h"

namespace Demon {

ThreadingManager::ThreadingManager() = default;

ThreadingManager::~ThreadingManager() {
    shutdown();
}

void ThreadingManager::init(uint32_t workerCount,
                            uint64_t affinityMask,
                            std::wstring_view namePrefix)
{
    DEMON_ASSERT(!m_running.load(), "ThreadingManager already initialised.");

    m_mainThreadId = std::this_thread::get_id();
    m_affinityMask = affinityMask;
    m_namePrefix   = std::wstring(namePrefix);
    m_running      = true;

    m_workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        auto worker = std::make_unique<Worker>();
        Worker* workerPtr = worker.get();
        std::wstring name = m_namePrefix + L" " + std::to_wstring(i);
        m_workers.emplace_back(std::move(worker));
        m_workers.back()->thread = std::thread([this, workerPtr, i, name]() {
            workerLoop(workerPtr, i, name);
        });
    }

    DEMON_LOG_INFO("ThreadingManager: started {} worker threads.", workerCount);
}

void ThreadingManager::shutdown()
{
    if (!m_running.exchange(false))
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.clear();
    }

    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker->thread.joinable())
            worker->thread.join();
    }
    m_workers.clear();
    m_inFlight.store(0);

    DEMON_LOG_INFO("ThreadingManager: shutdown complete.");
}

void ThreadingManager::submit(Task task)
{
    if (!task)
        return;

    if (!m_running.load()) {
        task();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.emplace_back(std::move(task));
    }
    m_cv.notify_one();
}

void ThreadingManager::waitIdle()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idleCv.wait(lock, [this] {
        return m_tasks.empty() && m_inFlight.load() == 0;
    });
}

void ThreadingManager::updateDebug(uint32_t stallWarningMs)
{
    if (!m_running.load() || stallWarningMs == 0)
        return;

    const uint64_t now = nowMs();
    bool hasPending = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        hasPending = !m_tasks.empty() || m_inFlight.load() > 0;
    }
    if (!hasPending)
        return;

    for (uint32_t i = 0; i < m_workers.size(); ++i) {
        Worker& worker = *m_workers[i];
        const uint64_t last = worker.lastHeartbeat.load();
        if (last == 0 || now - last < stallWarningMs)
            continue;

        const uint64_t lastWarn = worker.lastWarn.load();
        if (lastWarn != 0 && now - lastWarn < stallWarningMs)
            continue;

        worker.lastWarn.store(now);
        DEMON_LOG_WARN("ThreadingManager: worker {} stalled for {} ms.",
                       i, static_cast<uint32_t>(now - last));
    }
}

void ThreadingManager::setMainThread()
{
    m_mainThreadId = std::this_thread::get_id();
}

void ThreadingManager::setCurrentThreadName(std::wstring_view name)
{
#ifdef DEMON_PLATFORM_WINDOWS
    if (!name.empty()) {
        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
        static const auto setThreadDescription = reinterpret_cast<SetThreadDescriptionFn>(
            GetProcAddress(GetModuleHandleW(L"Kernel32.dll"), "SetThreadDescription"));
        if (setThreadDescription)
            setThreadDescription(GetCurrentThread(), name.data());
    }
#else
    (void)name;
#endif
}

void ThreadingManager::setCurrentThreadAffinity(uint64_t mask)
{
#ifdef DEMON_PLATFORM_WINDOWS
    if (mask != 0)
        SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask));
#else
    (void)mask;
#endif
}

void ThreadingManager::workerLoop(Worker* worker, uint32_t index, std::wstring name)
{
    setCurrentThreadName(name);
    setCurrentThreadAffinity(m_affinityMask);
    (void)index;

    worker->lastHeartbeat.store(nowMs());

    while (m_running.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_running.load() || !m_tasks.empty();
            });

            if (!m_running.load() && m_tasks.empty())
                break;

            task = std::move(m_tasks.front());
            m_tasks.pop_front();
            m_inFlight.fetch_add(1);
        }

        worker->lastHeartbeat.store(nowMs());
        if (task)
            task();
        worker->lastHeartbeat.store(nowMs());

        m_inFlight.fetch_sub(1);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_tasks.empty() && m_inFlight.load() == 0)
                m_idleCv.notify_all();
        }
    }
}

uint64_t ThreadingManager::nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace Demon
