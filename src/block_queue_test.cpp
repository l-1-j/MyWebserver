#include <iostream>
#include <thread>
#include "block_queue.h"

int main() {
    BlockQueue<int> q(10);   // 上限设成 10，故意设小，逼出"满"的等待

    // 生产者：塞 0~99 共 100 个
    std::thread producer([&q]() {
        for (int i = 0; i < 100; i++) {
            q.push(i);
        }
        std::cout << "生产者完成" << std::endl;
    });

    // 消费者：取 100 个，求和
    std::thread consumer([&q]() {
        long long sum = 0;
        for (int i = 0; i < 100; i++) {
            sum += q.pop();
        }
        std::cout << "消费者完成，总和 = " << sum << std::endl;
    });

    producer.join();
    consumer.join();
    return 0;
}
