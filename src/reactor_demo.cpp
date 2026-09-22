#include<iostream>
#include <thread>
#include <cstring>          // memset
#include <sys/socket.h>     // socket/bind/listen/accept
#include <netinet/in.h>     // sockaddr_in、htons、INADDR_ANY
#include <arpa/inet.h>      // inet 地址转换
#include <unistd.h>    
#include <chrono>
#include <string>    
#include<sys/epoll.h>
#include "thread_pool.h"
// 从 HTTP 请求里抠出路径（"GET /hello HTTP/1.1" → "/hello"）
std::string parsePath(const std::string& request) {
    size_t p1 = request.find(' ');              // 第1个空格位置
    size_t p2 = request.find(' ', p1 + 1);      // 第2个空格位置

    // 如果没找到两个空格（请求格式不对），就默认返回首页 "/"
    if (p1 == std::string::npos || p2 == std::string::npos) {
        return "/";
    }
    // 截取两个空格中间那段，就是路径
    return request.substr(p1 + 1, p2 - p1 - 1);
}

int main(){
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd<0){
        std::cout<<"创建socket失败"<<std::endl;
        return -1;
    }
    sockaddr_in addr;
    std::memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(9999);
    addr.sin_addr.s_addr=INADDR_ANY;
    bind(lfd,(sockaddr*)&addr,sizeof(addr));
    listen(lfd,128);
    int epfd=epoll_create(1024);
    epoll_event ev;
    ev.data.fd=lfd;
    ev.events=EPOLLIN;
    epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
    epoll_event events[1024];
    std::cout<<"服务器启动成功，正在监听9999端口"<<std::endl;
    ThreadPool pool(4);
    while(1){
        int n=epoll_wait(epfd,events,1024,-1);
        for(int i=0;i<n;i++){
            int fd=events[i].data.fd;
            if(fd==lfd){
                int cfd=accept(lfd,nullptr,nullptr);
                epoll_event ev2;
                ev2.data.fd=cfd;
                ev2.events=EPOLLIN;
                epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev2);
                std::cout<<"新客户连接"<<cfd<<std::endl;
            }
            else{
                char buf[1024]={0};
                int r=recv(fd,buf,sizeof(buf),0);
                if(r<=0){
                    std::cout<<"客户断开连接"<<fd<<std::endl;
                    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,nullptr);
                    close(fd);
                }
                else{
                    std::string msg(buf);
                    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,nullptr);
                    pool.addTask([fd,msg](){
                       std::string path=parsePath(msg);
                        std::string response;
                        if (path == "/") {
                            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                       "<html><body><h1>Welcome to the Home Page!</h1></body></html>";
                        } else if (path == "/hello") {
                            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                                       "<html><body><h1>Hello, World!</h1></body></html>";
                        } else {
                            response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                                       "<html><body><h1>404 Not Found</h1></body></html>";
                        }
                        send(fd,response.c_str(),response.size(),0);
                        close(fd);
                    });
                    
                

                }
            }
        }
    }
         
    return 0;
}


