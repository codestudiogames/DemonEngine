#pragma once
// ==============================================================================
//  DemonEngine::FileStream
//  Chunked async file streaming on a background thread.
// ==============================================================================
#include "DemonPCH.h"
#include <deque>

namespace Demon {

struct FileChunk {
    std::vector<uint8_t> data;
    uint64_t             offset = 0;
    bool                 eof = false;
};

class FileStream {
public:
    FileStream() = default;
    ~FileStream();

    FileStream(const FileStream&)            = delete;
    FileStream& operator=(const FileStream&) = delete;

    bool open(const std::filesystem::path& path,
              uint64_t chunkSize = 64 * 1024,
              uint32_t maxBuffered = 4);
    void close();

    [[nodiscard]] bool isOpen() const { return m_open; }
    [[nodiscard]] bool isEof()  const { return m_eof && m_queue.empty(); }
    [[nodiscard]] uint64_t size() const { return m_fileSize; }

    bool readNext(FileChunk& out);       // non-blocking: false if no chunk ready
    void waitNext(FileChunk& out);       // blocking

private:
    void workerLoop();

    std::filesystem::path m_path;
    uint64_t              m_chunkSize = 0;
    uint32_t              m_maxBuffered = 0;
    uint64_t              m_fileSize = 0;

    std::thread           m_worker;
    std::mutex            m_mutex;
    std::condition_variable m_cvProducer;
    std::condition_variable m_cvConsumer;
    std::deque<FileChunk> m_queue;

    std::atomic<bool>     m_running{false};
    std::atomic<bool>     m_open{false};
    std::atomic<bool>     m_eof{false};
};

} // namespace Demon
