#pragma once
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace ft {

class AsyncTaskQueue {
public:
    AsyncTaskQueue();
    ~AsyncTaskQueue();

    void enqueue(std::function<void()> task);

    template<class F>
    auto enqueueAndWait(F&& task) -> decltype(task()) {
        using R = decltype(task());
        auto pt = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
        std::future<R> fut = pt->get_future();
        enqueue([pt]() { (*pt)(); });
        return fut.get();
    }

    void stop();

private:
    void workerLoop();

    std::thread worker_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = true;
};

} // namespace ft

