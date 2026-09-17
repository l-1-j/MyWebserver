#include <iostream>
#include <cstring>          // memset
#include <sys/socket.h>     // socket/bind/listen/accept
#include <netinet/in.h>     // sockaddr_in、htons、INADDR_ANY
#include <arpa/inet.h>      // inet 地址转换
#include <unistd.h>         // close

int main() {
    // ① 创建 socket（买一部电话）
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        std::cout << "创建 socket 失败\n";
        return 1;
    }

    // ② 绑定 IP 和端口（登记电话号码）
    sockaddr_in address;                       // 描述"地址+端口"的结构体
    std::memset(&address, 0, sizeof(address)); // 先清零
    address.sin_family = AF_INET;              // 用 IPv4
    inet_pton(AF_INET, "0.0.0.0", &address.sin_addr);   // 监听所有网卡（0.0.0.0）
    address.sin_port = htons(8888);            // 端口 8888

    if (bind(listenfd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cout << "绑定失败\n";
        return 1;
    }

    // ③ 开始监听（电话开通，等客人来电）
    if (listen(listenfd, 5) < 0) {
        std::cout << "监听失败\n";
        return 1;
    }
    std::cout << "服务器已启动，正在监听 8888 端口...\n";

    // ④ 循环接受连接（一直营业，接待完一个客人继续等下一位）
    while (true) {
        sockaddr_in clientAddr;                    // 存"来的是谁"的信息
        socklen_t clientLen = sizeof(clientAddr);
        int connfd = accept(listenfd, (sockaddr*)&clientAddr, &clientLen);
        if (connfd < 0) {
            std::cout << "接受连接失败\n";
            continue;                              // 失败就继续等下一个
        }
        std::cout << "✅ 有人连接进来了！\n";

        // ⑤ 读客户端发来的数据
        char buffer[1024] = {0};                       // 缓冲区，装收到的数据
        int n = recv(connfd, buffer, sizeof(buffer), 0);   // 收数据
        if (n > 0) {
            std::cout << "收到数据：\n" << buffer << "\n";  // 打印收到的内容

            // ⑥ 构造一个 HTTP 响应并返回
            const char* response =
                "HTTP/1.1 200 OK\r\n"              // 状态行：成功
                "Content-Type: text/html; charset=utf-8\r\n"   // 头：内容是 HTML，编码是 UTF-8
                "\r\n"                             // 空行（头和正文的分隔）
                "<html><body><h1>你好，我的服务器！</h1></body></html>\n";  // 正文
            send(connfd, response, strlen(response), 0);   // 把响应发给客户端
        }

        // 送走这个客人（挂断这条连接），继续等下一位
        close(connfd);
    }
    // 注意：while(true) 是死循环，下面的代码执行不到。要停止服务器按 Ctrl+C。
    return 0;
}
