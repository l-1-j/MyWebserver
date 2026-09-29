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
#include<ctime>
const int MAX_FD=65536;
const int MAX_PORT=9999;
const int TIMEOUT=5;

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
    ep.addFD(lfd,EPOLLIN);
    Log::info("服务器成功启动，监听"+std::to_string(MAX_PORT)+"端口");
    while(true){
        int n=ep.wait(1000);
        if(n==0){
            time_t now=time(NULL);
            for(int i=0;i<MAX_FD;i++){
                if(users[i].isActive()&&now-users[i].getLastActive()>TIMEOUT){
                    users[i].closeConn();
                    Log::info("连接"+std::to_string(i)+"超时关闭");
                }
            }
            continue;
        }
        for(int i=0;i<n;i++){
            int fd=ep.getEventFD(i);
            if(fd==lfd){
                sockaddr_in clientAddr;
                socklen_t len=sizeof(clientAddr);
                int cfd=accept(lfd,(sockaddr*)&clientAddr,&len);
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET,&clientAddr.sin_addr,ipStr,sizeof(ipStr));
                users[cfd].init(cfd,&ep);
                users[cfd].setIp(ipStr);
                Log::info("新客户连接" + std::to_string(cfd) + "来自" + ipStr);
                ep.addFD(cfd,EPOLLIN);
            }
            else{
                ep.deFD(fd);
                pool.addTask([&users,fd](){
                    users[fd].process();
                });
            }
        }
    }
    return 0;
}