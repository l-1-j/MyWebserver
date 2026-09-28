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
    void setIp(const std::string &ip){ip_=ip;}
    private:
    std::string parsePath(const std::string&request)const;
    bool isKeepAlive(const std::string&request)const;
    std::string getContentType(const std::string&path)const;

    int fd_;
    Epoller*ep_;
    std::string ip_;

};  