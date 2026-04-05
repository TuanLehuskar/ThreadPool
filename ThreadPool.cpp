#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t numberOfThreads)
    : stopRunning(false)
{
    if (numberOfThreads == 0) {
        throw std::invalid_argument("ThreadPool requires at least one thread");
    }

    workers.reserve(numberOfThreads);

    for (size_t i = 0; i < numberOfThreads; ++i) {
        workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stopRunning = true;
    }

    cv.notify_all();

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::workerLoop()
{
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            cv.wait(lock, [this] {
                return stopRunning || !tasks.empty();
            });

            if (stopRunning && tasks.empty()) {
                return;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }

        task();
    }
}
