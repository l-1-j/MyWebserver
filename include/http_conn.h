#pragma once
#include<string>
#include<sys/socket.h>
#include<unistd.h>
class Epoller;
class HttpConn{
    public:
    explicit HttpConn():fd_(-1),ep_(nullptr){}
    void init(int fd,Epoller*ep);
    void process();
    private:
    std::string parsePath(const std::string&request)const;
    bool isKeepAlive(const std::string&request)const;
    int fd_;
    Epoller*ep_;
   
};