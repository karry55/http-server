#include "thread_pool.h"
#include <iostream>     // std::cerr（打印异常信息用）

ThreadPool::ThreadPool(size_t n) : stop_(false) {
    for (size_t i = 0; i < n; i++) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mtx_);
                    cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                // 兜底：任务里抛出的任何异常都不能逃出线程函数，
                // 否则 C++ 会调用 std::terminate() 直接杀掉整个进程。
                try {
                    task();
                } catch (const std::exception& e) {
                    std::cerr << "任务抛异常: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "任务抛未知异常" << std::endl;
                }
            }
        });
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
}
