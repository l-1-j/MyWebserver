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
#include<csignal>

const int MAX_FD=65536;
const int MAX_PORT=9999;
volatile sig_atomic_t g_stop=0;//退出标志

void handleSignal(int sig){
    g_stop=1;
}

int main(){
    //设置监听套接字
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
    ThreadPool pool(4);//启动线程池
    std::vector<HttpConn>users(MAX_FD);//用户连接数组
    Log::getInstance().init("log.txt");//启动日志
    //初始化连接池
    connection_pool::getInstance()->init("localhost", "webserver", "123456", "webserver", 3306, 8);
    std::cout << "连接池初始化完成，空闲连接：" << connection_pool::getInstance()->getFreeConn() << std::endl;

    ep.addFD(lfd,EPOLLIN);
    Log::getInstance().write(Log::INFO, "服务器成功启动，监听" + std::to_string(MAX_PORT) + "端口");
    TimerHeap &timer=TimerHeap::getInstance();//启动小根堆定时器
    //注册信号处理：Ctrl+C(SIGINT),kill(SIGTERM),触发优雅退出
    struct sigaction sa;
    std::memset(&sa,0,sizeof(sa));
    sa.sa_handler=handleSignal;//设置收到信号后的处理函数
    sigemptyset(&sa.sa_mask);//清空屏幕信号集
    sa.sa_flags=0;//行为标志设置为0，接收到打断信号后返回-1，errno=EINTR
    sigaction(SIGINT,&sa,nullptr);//参数1传入要处理的信号，参数2传入新的处理方式，参数3传出旧的处理方式
    sigaction(SIGTERM,&sa,nullptr);
    signal(SIGPIPE, SIG_IGN);

    while(!g_stop){
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
        if(n<0){
            if(errno==EINTR){
                continue;//被信号打断，回到顶部检查g_stop
            }
            Log::getInstance().write(Log::ERROR, "epoll_wait失败: " + std::string(strerror(errno)));
            break;//基本不会触发，异常状态保底。
        }
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
                if(!ep.addFD(cfd,EPOLLIN|EPOLLET|EPOLLONESHOT)){
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
    Log::getInstance().write(Log::INFO,"服务器正在关闭");
    Log::getInstance().stop();
    return 0;
}