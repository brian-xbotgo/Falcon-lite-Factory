#include "core/AsyncTaskQueue.h"
#include <cstdio>

namespace ft {

AsyncTaskQueue::AsyncTaskQueue(int pool_size) : max_pending_(pool_size * 2) {
    running_ = true;
    for (int i = 0; i < pool_size; ++i) {
        threads_.emplace_back(&AsyncTaskQueue::worker_loop, this);
    }
}

AsyncTaskQueue::~AsyncTaskQueue() { stop(); }

int AsyncTaskQueue::submit(WorkerTask task) {
    std::lock_guard<std::mutex> lk(mu_);
    if ((int)tasks_.size() >= max_pending_) return -1;
    tasks_.push_back(std::move(task));
    cv_.notify_one();
    return 0;
}

void AsyncTaskQueue::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

void AsyncTaskQueue::worker_loop() {
    while (running_) {
        WorkerTask task;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return !tasks_.empty() || !running_; });
            if (!running_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try {
            task.work();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[AsyncTaskQueue] exception: %s\n", e.what());
        }
    }
}

} // namespace ft
