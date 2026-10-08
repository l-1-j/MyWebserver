#include <iostream>
#include "sql_connection_pool.h"

int main() {
    // 1. 初始化连接池：8 个连接
    connection_pool* pool = connection_pool::getInstance();
    pool->init("localhost", "webserver", "123456", "webserver", 3306, 8);
    std::cout << "连接池初始化完成，空闲连接：" << pool->getFreeConn() << std::endl;

    // 2. RAII 借一个连接
    MYSQL* conn = nullptr;
    {
        connectionRAII raii(&conn, pool);
        std::cout << "借到连接，当前空闲：" << pool->getFreeConn() << std::endl;

        // 3. 插入一条测试数据
        const char* insertSql =
            "INSERT INTO users(username, passwd) VALUES('test', '123')";
        if (mysql_query(conn, insertSql) == 0) {
            std::cout << "插入成功" << std::endl;
        } else {
            std::cout << "插入失败：" << mysql_error(conn) << std::endl;
        }

        // 4. 查回来验证
        const char* selectSql =
            "SELECT username, passwd FROM users WHERE username='test'";
        if (mysql_query(conn, selectSql) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                std::cout << "查到：username=" << row[0]
                          << ", passwd=" << row[1] << std::endl;
            }
            mysql_free_result(res);
        }
    }
    // 离开作用域，RAII 自动还连接
    std::cout << "连接已归还，当前空闲：" << pool->getFreeConn() << std::endl;

    return 0;
}
