#include "thread_pool.h"

// 构造函数：提前创建好 numThreads 个工作线程（提前雇好厨师）
ThreadPool::ThreadPool(int numThreads) : stop_(false) {
    for (int i = 0; i < numThreads; ++i) {
        // 每个线程都执行 workerLoop 这个函数（this 表示"对我这个对象操作"）
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

// 析构函数：让所有线程安全退出
ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mtx_);
        stop_ = true;          // 置停止标志
    }
    cv_.notify_all();          // 叫醒所有睡着的线程，让它们看到 stop_ 后退出

    // 回收所有线程（等它们都结束）
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

// 提交任务：放进队列，然后叫醒一个线程来干
void ThreadPool::addTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(mtx_);
        tasks_.push(std::move(task));   // 把任务放进队列（加锁保护）
    }
    cv_.notify_one();                    // 摇铃，叫醒一个睡着的厨师
}

// 工作线程的主循环
void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mtx_);
            // 睡觉等：直到"有任务"或"线程池要关闭"
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

            // 如果线程池要关闭，且没有剩余任务了，就退出循环（厨师下班）
            if (stop_ && tasks_.empty()) {
                return;
            }

            // 取出队首任务
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        // 在锁外执行任务（别拿着锁干活，会拖慢别人）
        task();
    }
}
