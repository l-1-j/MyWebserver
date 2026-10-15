#pragma once
#include<string>
#include<sys/socket.h>
#include<unistd.h>
#include<ctime>
class Epoller;
class HttpConn{
    public:
    explicit HttpConn():fd_(-1),ep_(nullptr){}
    void init(int fd,Epoller*ep);
    void process();
    void setIp(const std::string &ip){ip_=ip;}
    void closeConn();
    bool isActive()const {return fd_!=-1;}
    time_t getLastActive()const {return lastActive_;}

    private:
    std::string sha256(const std::string&str);
    std::string parseBody(const std::string&request)const;
    std::string parsePath(const std::string&request)const;
    std::string parseParam(const std::string& body, const std::string& key) const;
    bool isKeepAlive(const std::string&request)const;
    std::string getContentType(const std::string&path)const;
    
    int fd_;
    Epoller*ep_;
    std::string ip_;
    time_t lastActive_;

};  