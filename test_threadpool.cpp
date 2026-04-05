#include "ThreadPool.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <vector>

// Basic: enqueue returns correct result
TEST(ThreadPoolTest, BasicReturnValue)
{
    ThreadPool pool(2);
    auto future = pool.enqueue([](int a, int b) { return a + b; }, 3, 4);
    EXPECT_EQ(future.get(), 7);
}

// Multiple tasks: all execute and results are correct
TEST(ThreadPoolTest, MultipleTasksReturnCorrectResults)
{
    ThreadPool pool(4);
    const int N = 100;

    std::vector<std::future<int>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i) {
        futures.emplace_back(pool.enqueue([](int x) { return x * x; }, i));
    }

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

// Concurrency: tasks actually run in parallel
TEST(ThreadPoolTest, ParallelExecution)
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    const int N = 8;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < N; ++i) {
        futures.emplace_back(pool.enqueue([&counter] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    auto start = std::chrono::steady_clock::now();
    for (auto& f : futures) f.get();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_EQ(counter.load(), N);

    // 8 tasks * 50ms / 4 threads = ~100ms if truly parallel
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_LT(ms, 300);
}

// Task with no return value (void)
TEST(ThreadPoolTest, VoidTask)
{
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.emplace_back(pool.enqueue([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (auto& f : futures) f.get();
    EXPECT_EQ(counter.load(), 10);
}

// Exception in task propagates through future
TEST(ThreadPoolTest, ExceptionPropagation)
{
    ThreadPool pool(2);

    auto future = pool.enqueue([]() -> int {
        throw std::runtime_error("task error");
        return 0;
    });

    EXPECT_THROW({
        future.get();
    }, std::runtime_error);

    // Also verify the message
    auto future2 = pool.enqueue([]() -> int {
        throw std::runtime_error("task error");
        return 0;
    });

    try {
        future2.get();
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "task error");
    }
}

// No crash or hang after pool is destroyed
TEST(ThreadPoolTest, CleanShutdown)
{
    std::future<int> future;
    {
        ThreadPool pool(2);
        future = pool.enqueue([] { return 1; });
        EXPECT_EQ(future.get(), 1);
    }
    // pool destroyed — no hang, no crash
}

// Single thread pool — tasks serialize in FIFO order
TEST(ThreadPoolTest, SingleThreadFifoOrdering)
{
    ThreadPool pool(1);
    std::vector<int> order;
    std::mutex order_mutex;

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 5; ++i) {
        futures.emplace_back(pool.enqueue([&order, &order_mutex, i] {
            std::unique_lock<std::mutex> lock(order_mutex);
            order.push_back(i);
        }));
    }

    for (auto& f : futures) f.get();

    ASSERT_EQ((int)order.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(order[i], i);
    }
}

// Hardware concurrency default constructor
TEST(ThreadPoolTest, HardwareConcurrencyConstructor)
{
    ThreadPool pool(std::thread::hardware_concurrency());
    auto future = pool.enqueue([] { return 42; });
    EXPECT_EQ(future.get(), 42);
}

// wait() blocks until all tasks complete
TEST(ThreadPoolTest, WaitBlocksUntilDone)
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 20; ++i) {
        pool.enqueue([&counter] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.wait();
    EXPECT_EQ(counter.load(), 20);
}

// thread_count() returns the correct number of threads
TEST(ThreadPoolTest, ThreadCount)
{
    ThreadPool pool(4);
    EXPECT_EQ(pool.thread_count(), 4u);
}

// queue_size() reflects pending task count
TEST(ThreadPoolTest, QueueSize)
{
    ThreadPool pool(1);

    std::mutex block_mutex;
    std::condition_variable started_cv;
    bool started = false;

    std::unique_lock<std::mutex> block_lock(block_mutex);

    // First task signals that it has started, then blocks
    pool.enqueue([&] {
        {
            std::unique_lock<std::mutex> lock(block_mutex);
            started = true;
            started_cv.notify_one();
        }
        // Wait until the test releases block_lock
        std::unique_lock<std::mutex> lock(block_mutex);
    });

    // Wait until the worker has actually picked up and started the first task
    started_cv.wait(block_lock, [&] { return started; });

    // Now the queue is empty — enqueue 3 more while worker is blocked
    for (int i = 0; i < 3; ++i) {
        pool.enqueue([] {});
    }

    EXPECT_EQ(pool.queue_size(), 3u);

    block_lock.unlock();
    pool.wait();
    EXPECT_EQ(pool.queue_size(), 0u);
}
