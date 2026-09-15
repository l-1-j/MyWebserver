#include <iostream>
#include <thread>
#include <chrono>
#include <string>

// 每个"厨师"要做的活：睡 seconds 秒，然后报告做好了
void cook(const std::string& dish, int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    std::cout << "✅ " << dish << " 做好了！\n";
}

int main() {
    std::cout << "===== 单线程：一道一道做 =====\n";
    auto start = std::chrono::steady_clock::now();
    cook("番茄炒蛋", 2);   // 先做这道，花 2 秒
    cook("红烧肉", 2);     // 再做这道，又花 2 秒
    auto end = std::chrono::steady_clock::now();
    std::cout << "单线程耗时: "
              << std::chrono::duration_cast<std::chrono::seconds>(end - start).count()
              << " 秒\n\n";

    std::cout << "===== 多线程：两个厨师同时做 =====\n";
    start = std::chrono::steady_clock::now();
    std::thread chef1(cook, "番茄炒蛋", 2);   // 开一个线程（厨师1）
    std::thread chef2(cook, "红烧肉", 2);     // 开一个线程（厨师2）
    chef1.join();   // 等厨师1做完
    chef2.join();   // 等厨师2做完
    end = std::chrono::steady_clock::now();
    std::cout << "多线程耗时: "
              << std::chrono::duration_cast<std::chrono::seconds>(end - start).count()
              << " 秒\n";

    return 0;
}
