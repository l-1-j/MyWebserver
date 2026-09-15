#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

// 线程池：提前创建固定数量的工作线程，反复复用它们执行任务
class ThreadPool {
public:
    // 构造函数：创建 numThreads 个工作线程
    explicit ThreadPool(int numThreads);

    // 析构函数：通知所有线程退出，并回收它们
    ~ThreadPool();

    // 禁止拷贝（线程池只能有一个，不能复制）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 向线程池提交一个任务（任务 = 一个无参无返回的函数）
    void addTask(std::function<void()> task);

private:
    // 工作线程的主循环：反复从队列取任务并执行
    void workerLoop();

    std::vector<std::thread> workers_;           // 工作线程（厨师们）
    std::queue<std::function<void()>> tasks_;    // 任务队列（待办的活）
    std::mutex mtx_;                             // 锁：保护任务队列
    std::condition_variable cv_;                 // 条件变量（铃铛）：通知有任务
    bool stop_ = false;                          // 停止标志：为 true 时线程退出
};
