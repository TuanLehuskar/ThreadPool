# ThreadPool

C++17 thread pool. Requires only the standard library.

## Build

```bash
cmake -B build
cmake --build build
```

gTest is fetched automatically at configure time.

## Run tests

```bash
./build/threadpool_test
./build/threadpool_test --gtest_filter="ThreadPoolTest.ParallelExecution"
```

## Usage

```cpp
ThreadPool pool(4);

// Submit a task, get a future for the result
std::future<int> result = pool.enqueue([](int a, int b) {
    return a + b;
}, 3, 4);

int value = result.get();

// Block until all queued tasks finish
pool.wait();

// Introspection
size_t threads = pool.thread_count(); // number of worker threads
size_t pending = pool.queue_size();   // number of tasks waiting in queue
```

`enqueue` accepts any callable and forwards its arguments. The return value is
accessible through the future. Throws `std::runtime_error` if the pool is
already destroyed.

## Design notes

### Constructor

Spawns `numberOfThreads` worker threads, each running `workerLoop`. Throws
`std::invalid_argument` if `numberOfThreads` is 0. `workers.reserve` is called
first to avoid reallocation while threads are being emplaced.

### workerLoop

Each worker blocks on `cv.wait` with the predicate `stopRunning || !tasks.empty()`.
The predicate guards against spurious wakeups — a thread that wakes up without
either condition being true goes back to sleep immediately.

Once woken with a task available, the worker moves the task out of the queue,
increments `active_tasks`, then releases the lock before calling `task()`.
Releasing the lock before execution is important: holding it during `task()`
would serialize all workers and defeat the purpose of the pool. After the task
returns, the worker decrements `active_tasks` and notifies `done_cv` so that
any caller blocked in `wait()` can re-check the idle condition.

### Destructor

Sets `stopRunning = true` under the lock, then calls `cv.notify_all()` to wake
every waiting thread. Workers that find the queue empty after waking will exit.
Workers mid-task finish their current task before checking the flag again. All
threads are joined before the destructor returns, so the pool never outlives its
threads.

The lock scope around `stopRunning = true` is intentional — without it, a worker
could read `stopRunning` between the assignment and `notify_all`, miss the
notification, and sleep forever.

### wait

Blocks the calling thread until `tasks.empty() && active_tasks == 0`. Uses a
separate `done_cv` rather than the main `cv` to avoid interfering with workers
waiting for new tasks. Safe to call at any time, including when the queue is
already empty.

### thread_count / queue_size

`thread_count()` returns `workers.size()` with no locking needed — the vector
is write-once after construction. `queue_size()` locks `queue_mutex` (declared
`mutable`) so it can be called on a `const` pool reference.

### enqueue

The queue stores `std::function<void()>`, so tasks must erase their return type
before being pushed. `enqueue` does this by wrapping the callable in a
`packaged_task`, extracting a future first, then pushing a `void()` lambda that
captures the `packaged_task` by `shared_ptr`. The `shared_ptr` keeps the task
alive across the enqueue/execute boundary without requiring the queue to know
about the return type.

Arguments are packed into a `std::tuple` at call time and unpacked with
`std::apply` inside the lambda. This avoids `std::bind` and preserves move
semantics for rvalue arguments.
