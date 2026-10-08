#pragma once
#include <mysql/mysql.h>
#include <string>
#include <list>
#include <mutex>
#include <semaphore.h>

// 数据库连接池（单例）
class connection_pool {
public:
    static connection_pool* getInstance();
    void init(const std::string& url, const std::string& user,
              const std::string& passwd, const std::string& dbname,
              int port, int maxConn);
    MYSQL* getConnection();                 // 借一个连接
    bool releaseConnection(MYSQL* conn);    // 还一个连接
    int getFreeConn();                      // 看还有几个空闲连接
    void destroyPool();                     // 销毁所有连接

private:
    connection_pool();
    ~connection_pool();
    connection_pool(const connection_pool&) = delete;
    connection_pool& operator=(const connection_pool&) = delete;

    std::string url_;       // 主机地址 localhost
    std::string user_;      // 用户名 webserver
    std::string passwd_;    // 密码 123456
    std::string dbname_;    // 库名 webserver
    int port_;              // 端口 3306
    int maxConn_;           // 最大连接数
    int freeConn_;          // 当前空闲连接数
    std::list<MYSQL*> connList_;  // 空闲连接的链表（池子本体）
    std::mutex mtx_;        // 保护 connList_ 的锁
    sem_t sem_;             // 信号量：记录"还剩几个连接可用"
};

// RAII 封装：借连接 / 自动还连接
class connectionRAII {
public:
    connectionRAII(MYSQL** conn, connection_pool* pool);
    ~connectionRAII();
private:
    MYSQL* conn_;
    connection_pool* pool_;
};
