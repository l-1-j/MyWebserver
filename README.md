# MyWebServer

Linux 下基于 C++ 的高性能 HTTP 服务器（学习中）。

## 技术栈
- C++17
- epoll + 非阻塞 IO + Reactor
- 线程池

## 构建与运行
```bash
cmake -S . -B build
cmake --build build
./build/server
```
