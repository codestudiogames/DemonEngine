#include "WorkerPipe.h"
#include "core/Logger.h"

#include <string>

#ifndef DEMON_WORKER_NAME
#define DEMON_WORKER_NAME "DemonWorker"
#endif

int main(int argc, char** argv)
{
    Demon::Logger::init();

    std::string processName = DEMON_WORKER_NAME;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if ((arg == "--worker-name" || arg == "--name") && i + 1 < argc)
            processName = argv[++i];
    }

    DEMON_LOG_INFO("{} started as named-pipe background worker.", processName);
    const int result = Demon::WorkerIPC::runNamedPipeWorker(processName);
    DEMON_LOG_INFO("{} stopped.", processName);
    return result;
}
