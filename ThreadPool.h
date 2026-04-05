#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <vector>

class ThreadPool {

public:
    explicit ThreadPool(size_t numberOfThreads);

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    void wait();

    size_t thread_count() const noexcept;
    size_t queue_size() const;

private:
    void workerLoop();

    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
    mutable std::mutex                queue_mutex;
    std::condition_variable           cv;
    std::condition_variable           done_cv;
    std::atomic<size_t>               active_tasks{0};
    bool                              stopRunning;
};

template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    std::tuple<std::decay_t<Args>...> args_tuple = std::make_tuple(std::forward<Args>(args)...);

    auto wrapper_lambda = [
        func       = std::forward<F>(f),
        args_tuple = std::move(args_tuple)
    ]() mutable -> return_type {
        return std::apply(std::move(func), std::move(args_tuple));
    };

    std::shared_ptr<std::packaged_task<return_type()>> task_ptr =
        std::make_shared<std::packaged_task<return_type()>>(std::move(wrapper_lambda));

    std::future<return_type> result_future = task_ptr->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if (stopRunning) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        tasks.emplace([task_ptr]() {
            (*task_ptr)();
        });
    }

    cv.notify_one();

    return result_future;
}
