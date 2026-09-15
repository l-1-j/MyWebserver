#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>          // 新增：锁的头文件
#include "thread_pool.h"

std::mutex coutMutex;   // 新增：保护 cout 的锁（所有线程共用这一把）

int main() {
    ThreadPool pool(4);   // 创建线程池：4 个工作线程（4 个厨师）

    // 提交 8 个任务，每个任务打印自己的编号和"是谁干的"（线程 id）
    for (int i = 1; i <= 8; ++i) {
        pool.addTask([i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300)); // 假装干点活
            std::lock_guard<std::mutex> guard(coutMutex);   // 新增：打印前加锁
            std::cout << "任务 " << i << " 完成，线程 id = "
                      << std::this_thread::get_id() << "\n";
        });
    }

    std::cout << "已提交 8 个任务，线程池开始工作...\n";
    // 注意：pool 在 main 结束时会析构，析构函数会"等所有任务跑完再退出"，
    // 所以这里不用手动等待，程序会自动等任务全部完成。
    return 0;
}
