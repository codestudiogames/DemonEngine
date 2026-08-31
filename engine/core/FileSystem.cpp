// ==============================================================================
//  DemonEngine::FileStream
// ==============================================================================
#include "FileSystem.h"
#include "Logger.h"
#include <utility>

namespace Demon {

FileStream::~FileStream()
{
    close();
}

bool FileStream::open(const std::filesystem::path& path,
                      uint64_t chunkSize,
                      uint32_t maxBuffered)
{
    close();

    if (!std::filesystem::exists(path)) {
        DEMON_LOG_WARN("FileStream: file not found: {}", path.string());
        return false;
    }

    m_path = path;
    m_chunkSize = std::max<uint64_t>(4096, chunkSize);
    m_maxBuffered = std::max<uint32_t>(1, maxBuffered);

    std::error_code ec;
    m_fileSize = std::filesystem::file_size(m_path, ec);
    if (ec) {
        DEMON_LOG_WARN("FileStream: failed to read size for {}", m_path.string());
        m_fileSize = 0;
    }

    m_running = true;
    m_open = true;
    m_eof = false;
    m_worker = std::thread([this]() { workerLoop(); });
    return true;
}

void FileStream::close()
{
    if (!m_open.exchange(false))
        return;

    m_running = false;
    m_cvProducer.notify_all();
    m_cvConsumer.notify_all();

    if (m_worker.joinable())
        m_worker.join();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }
    m_eof = true;
}

bool FileStream::readNext(FileChunk& out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.empty())
        return false;

    out = std::move(m_queue.front());
    m_queue.pop_front();
    m_cvProducer.notify_one();
    return true;
}

void FileStream::waitNext(FileChunk& out)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cvConsumer.wait(lock, [this] {
        return !m_queue.empty() || !m_running.load();
    });

    if (m_queue.empty())
        return;

    out = std::move(m_queue.front());
    m_queue.pop_front();
    m_cvProducer.notify_one();
}

void FileStream::workerLoop()
{
    std::ifstream file(m_path, std::ios::binary);
    if (!file.is_open()) {
        DEMON_LOG_ERROR("FileStream: failed to open {}", m_path.string());
        m_running = false;
        m_eof = true;
        return;
    }

    uint64_t offset = 0;
    while (m_running.load()) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cvProducer.wait(lock, [this] {
                return !m_running.load() || m_queue.size() < m_maxBuffered;
            });
        }

        if (!m_running.load())
            break;

        FileChunk chunk;
        chunk.offset = offset;
        chunk.data.resize(static_cast<size_t>(m_chunkSize));
        file.read(reinterpret_cast<char*>(chunk.data.data()), static_cast<std::streamsize>(m_chunkSize));
        const std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0) {
            chunk.data.clear();
            chunk.eof = true;
        } else {
            chunk.data.resize(static_cast<size_t>(bytesRead));
            offset += static_cast<uint64_t>(bytesRead);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.emplace_back(std::move(chunk));
        }
        m_cvConsumer.notify_one();

        if (file.eof()) {
            m_eof = true;
            break;
        }
    }

    m_running = false;
    m_cvConsumer.notify_all();
}

} // namespace Demon
