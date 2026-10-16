#pragma once
#include<string>
#include<sys/socket.h>
#include<unistd.h>
#include<ctime>
class Epoller;

// 待关闭 fd 队列：worker 线程把要关闭的 fd 放进队列，
// 由主线程在事件循环里统一 close，避免主线程 accept 与 worker close 的数据竞争
void addCloseFd(int fd);
bool getCloseFd(int& fd);

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