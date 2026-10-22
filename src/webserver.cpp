#include<iostream>
#include<thread>
#include<cstring>  
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<vector>
#include"epoller.h"
#include"thread_pool.h"
#include"http_conn.h"
#include"log.h"
#include"sql_connection_pool.h"
#include"timer.h"
#include<ctime>
#include<cerrno>

const int MAX_FD=65536;
const int MAX_PORT=9999;


int main(){
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd<0){
        std::cout<<"创建socket失败"<<std::endl;
        return -1;
    }
    sockaddr_in addr;
    std::memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(MAX_PORT);
    addr.sin_addr.s_addr=INADDR_ANY;
    bind(lfd,(sockaddr*)&addr,sizeof(addr));
    listen(lfd,128);
    Epoller ep;
    ThreadPool pool(4);
    std::vector<HttpConn>users(MAX_FD);
    Log::getInstance().init("log.txt");
    connection_pool::getInstance()->init("localhost", "webserver", "123456", "webserver", 3306, 8);
    std::cout << "连接池初始化完成，空闲连接：" << connection_pool::getInstance()->getFreeConn() << std::endl;

    ep.addFD(lfd,EPOLLIN);
    Log::getInstance().write(Log::INFO, "服务器成功启动，监听" + std::to_string(MAX_PORT) + "端口");
    TimerHeap &timer=TimerHeap::getInstance();//启动小根堆定时器
    while(true){
        //主线程统一关闭交还的fd,这一块主要处理worker线程里面断开连接的fd
        int closeFd;
        while(getCloseFd(closeFd)){
            if(closeFd>=0&&closeFd<MAX_FD){
                users[closeFd].closeConn();
                timer.delTimer(closeFd);
            }
        }
        //触发所以空闲超时的连接
        std::vector<int>expired;
        timer.tick(expired);
        //这一块主要处理连接开始就超时的连接
        for(int fd:expired){
            if(fd>=0&&fd<MAX_FD&&users[fd].isActive()){
                users[fd].closeConn();
                Log::getInstance().write(Log::INFO, "连接" + std::to_string(fd) + "超时关闭");
            }
        }
        //epoll_wait 超时时间等于最近一个定时器的到时时间（空堆就-1，一直阻塞）
        int timeout=-1;
        time_t topExpire=timer.topExpire();
        if(topExpire!=-1){
            timeout=(topExpire-time(NULL))*1000;
            if(timeout<0) timeout=0;
        }
        int n=ep.wait(timeout);
        for(int i=0;i<n;i++){
            int fd=ep.getEventFD(i);
            if(fd==lfd){
                sockaddr_in clientAddr;
                socklen_t len=sizeof(clientAddr);
                int cfd=accept(lfd,(sockaddr*)&clientAddr,&len);
                if(cfd<0){
                    Log::getInstance().write(Log::ERROR, "accept失败: " + std::string(strerror(errno)));
                    continue;
                }
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET,&clientAddr.sin_addr,ipStr,sizeof(ipStr));
                users[cfd].init(cfd,&ep);
                users[cfd].setIp(ipStr);
                Log::getInstance().write(Log::INFO, "新客户连接" + std::to_string(cfd) + "来自" + ipStr);
                if(!ep.addFD(cfd,EPOLLIN|EPOLLONESHOT)){
                    Log::getInstance().write(Log::ERROR, "addFD失败 fd=" + std::to_string(cfd) + ": " + std::string(strerror(errno)));
                }
                timer.addTimer(cfd,time(NULL)+TIMEOUT);
            }
            else{
               //EPOLLONESHOT：只触发一次，必须手动重置事件
                timer.delTimer(fd);//交给worker处理，暂停计时
                pool.addTask([&users,fd](){
                    users[fd].process();
                });
            }
        }
    }
    return 0;
}