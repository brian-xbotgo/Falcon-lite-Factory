#include "core/AsyncTaskQueue.h"

namespace ft {

AsyncTaskQueue::AsyncTaskQueue() : worker_(&AsyncTaskQueue::workerLoop, this) {}

AsyncTaskQueue::~AsyncTaskQueue() { stop(); }

void AsyncTaskQueue::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(task));
    }
    cv_.notify_one();
}

void AsyncTaskQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void AsyncTaskQueue::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) break;
            task = std::move(queue_.front());
            queue_.pop();
        }
        task();
    }
}

} // namespace ft
