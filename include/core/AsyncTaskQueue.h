#pragma once

#include <thread>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace ft {

struct WorkerTask {
    std::function<void()> work;
};

class AsyncTaskQueue {
public:
    explicit AsyncTaskQueue(int pool_size);
    ~AsyncTaskQueue();

    AsyncTaskQueue(const AsyncTaskQueue&) = delete;
    AsyncTaskQueue& operator=(const AsyncTaskQueue&) = delete;

    int submit(WorkerTask task);
    void stop();

private:
    void worker_loop();

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<WorkerTask> tasks_;
    int max_pending_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};
};

} // namespace ft
