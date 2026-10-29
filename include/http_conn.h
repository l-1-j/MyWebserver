#pragma once
#include<string>
#include<sys/socket.h>
#include<unistd.h>
#include<ctime>
class Epoller;

// 主状态机：解析到哪一步了
enum CHECK_STATE{ CHECK_STATE_REQUESTLINE=0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
// 从状态机：读一行的结果
enum LINE_STATUS{ LINE_OK=0, LINE_BAD, LINE_OPEN };
// 请求解析结果
enum HTTP_CODE{ NO_REQUEST=0, GET_REQUEST, BAD_REQUEST, INTERNAL_ERROR };

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
    std::string parseCookie(const std::string&request)const;
    std::string parseParam(const std::string& body, const std::string& key) const;
    bool isKeepAlive(const std::string&request)const;
    std::string getContentType(const std::string&path)const;

    // HTTP 解析状态机
    LINE_STATUS parseLine();
    HTTP_CODE processRead();
    HTTP_CODE parseRequestLine(const std::string& line);
    HTTP_CODE parseHeaders(const std::string& line);
    HTTP_CODE parseContent();
    
    int fd_;
    Epoller*ep_;
    std::string ip_;
    time_t lastActive_;

    // 读缓冲区 + 状态机状态
    std::string readBuf_;                          // 攒住半包数据
    CHECK_STATE checkState_=CHECK_STATE_REQUESTLINE;
    size_t checkIdx_=0;                            // 已解析到的位置
    size_t lineStart_=0;
    size_t lineEnd_=0;
    int contentLength_=0;
    // 解析出的请求信息
    std::string method_;
    std::string path_;
    std::string body_;

};  