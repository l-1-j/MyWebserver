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
    ep.addFD(lfd,EPOLLIN);
    std::cout<<"服务器成功启动，监听"<<MAX_PORT<<"端口"<<std::endl;
    while(true){
        int n=ep.wait(-1);
        for(int i=0;i<n;i++){
            int fd=ep.getEventFD(i);
            if(fd==lfd){
                int cfd=accept(lfd,nullptr,nullptr);
                users[cfd].init(cfd,&ep);
                std::cout<<"新客户连接"<<cfd<<std::endl;
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