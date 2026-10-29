# MyWebServer

Linux 下基于 C++17 的高性能 HTTP 服务器（学习项目，参考 TinyWebServer 重写）。

## 功能特性

- **epoll + Reactor + 线程池**：主线程监听事件，worker 线程池处理业务
- **非阻塞 IO + 边缘触发（ET）**：socket 设 `O_NONBLOCK`，epoll 用 `EPOLLET`，一次触发读到 EAGAIN 为止
- **HTTP/1.1 keep-alive**：支持长连接复用
- **HTTP 解析状态机（主从状态机）**：请求行 → 请求头 → 请求体逐段解析，解决 TCP 半包 / 粘包
- **EPOLLONESHOT**：一次只触发一个 fd，避免多线程同时读写同一连接
- **注册 / 登录**：MySQL 存储用户，密码 SHA-256 加盐存储，SQL 预处理防注入
- **Session + Cookie 登录态**：登录后 302 重定向 + `Set-Cookie`，30 分钟过期，惰性清理
- **异步日志**：阻塞队列 + 后台写线程，主线程不打日志 IO
- **小根堆定时器**：统一清理超时连接
- **信号处理优雅退出**：收到信号后关闭连接、刷日志、join 线程，不丢日志
- **连接池**：RAII 管理 MySQL 连接，减少连接建立开销

## 技术栈

- C++17、CMake
- epoll（边缘触发 ET + EPOLLONESHOT）、socket、Reactor 模式
- 线程池、互斥锁 / 条件变量、阻塞队列
- MySQL（mysqlclient）、OpenSSL（SHA-256）
- 小根堆定时器

## 构建与运行

依赖：Linux、MySQL 开发库、OpenSSL、CMake ≥ 3.16。

```bash
# 1. 初始化数据库
mysql -u root -p < sql/init.sql

# 2. 编译
cmake -S . -B build
cmake --build build

# 3. 运行
./build/webserver
```

## 测试

注册 / 登录流程：
1. 浏览器访问 `http://localhost:端口/register` 注册
2. 访问 `/login` 登录，成功后自动 302 跳转到 `/welcome`，页面显示「欢迎，用户名」

压测（wrk，keep-alive）：

```
wrk -t4 -c100 -d30s http://127.0.0.1:8080/hello
```

实测约 **1700 QPS**（单机、日志逐请求打印、LT 阻塞 IO 下，ET 改造后待重新压测）。

## 项目结构

```
include/         头文件（http_conn、epoller、thread_pool、log、timer、session 等）
src/             源码（webserver 主流程、各模块实现）
sql/             建表脚本
www/             静态文件
```

## 已知不足（待优化）

- **accept EMFILE**：文件描述符耗尽时 accept 未处理，会忙转
- **send 返回值未完全处理**：`send` 可能未一次写完（大响应需循环发送）
- **日志逐请求打印**：每个请求都打一条日志，是 QPS 的主要瓶颈之一，可改成抽样或分级

## 学习路线记录

从 socket → epoll → Reactor → 线程池 → 连接池 → 定时器 → 异步日志 → Session → 非阻塞 + ET → HTTP 状态机，一步步搭建，对应 26 个 commit（D1–D26）。
