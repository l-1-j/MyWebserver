#include <iostream>
#include <cstring>          // memset、strlen
#include <sys/socket.h>     // socket/bind/listen/accept
#include <arpa/inet.h>      // inet_pton、htons
#include <sys/epoll.h>      // epoll
#include <unistd.h>         // close

int main() {
    // ① 创建监听 socket
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        std::cout << "socket error" << std::endl;
        return -1;
    }

    // ② 绑定 IP 和端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);
    inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);
    bind(lfd, (struct sockaddr*)&addr, sizeof(addr));

    // ③ 监听
    listen(lfd, 128);

    // ④ 创建 epoll 实例（前台接待员）
    int epfd = epoll_create(1);
    if (epfd < 0) {
        std::cout << "epoll_create error" << std::endl;
        return -1;
    }

    // ⑤ 把 listenfd 注册进 epoll
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    // ⑥ 接待员用来汇报的"纸条本"
    epoll_event events[1024];
    int size = sizeof(events) / sizeof(events[0]);

    std::cout << "epoll 服务器已启动" << std::endl;

    while (true) {
        // 等：谁有动静就返回
        int n = epoll_wait(epfd, events, size, -1);

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == lfd) {
                // 情况①：listenfd 有动静 = 新客人上门
                int cfd = accept(lfd, nullptr, nullptr);
                if (cfd < 0) {
                    std::cout << "accept error" << std::endl;
                    continue;
                }

                // 把新客人也交给接待员盯着
                epoll_event ev2;
                ev2.events = EPOLLIN;
                ev2.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev2);
                std::cout << "新客户连接 " << cfd << std::endl;
            } else {
                // 情况②：普通连接有动静 = 客人发数据了
                char buf[1024] = {0};
                int r = recv(fd, buf, sizeof(buf), 0);

                if (r <= 0) {
                    // 客人挂断或出错，先移除再关闭
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                } else {
                    std::cout << "收到客户 " << fd << " 数据：" << buf << std::endl;

                    // 回一个完整的 HTTP 响应
                    const char* response =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "\r\n"
                        "<html><body><h1>你好，epoll 服务器！</h1></body></html>\n";
                    send(fd, response, strlen(response), 0);
                }
            }
        }
    }

    return 0;
}
