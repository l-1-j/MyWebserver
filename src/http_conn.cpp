#include"http_conn.h"
#include"epoller.h"
#include <fstream>
#include <sstream>
void HttpConn::init(int fd,Epoller*ep){
    fd_=fd;
    ep_=ep;
}
void HttpConn::process(){
    char buff[1024]={0};
    int r=recv(fd_,buff,sizeof(buff),0);
    if(r<=0){
        close(fd_);
        return;
    }
    else{
        std::string msg(buff);
        bool keepAlive=isKeepAlive(msg);
        std::string path=parsePath(msg);
        std::string body;
        std::string status="200 OK";
        std::string contentType="text/html";   // 默认类型
        if(path=="/") {
            body="<html><body><h1>Welcome to the Home Page!</h1></body></html>";
        }
        else if(path=="/hello"){
            body="<html><body><h1>Hello, World!</h1></body></html>";
        }
        else{
            // 尝试当静态文件：www/ + 路径
            std::ifstream file("www" + path, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                body = ss.str();
                contentType = getContentType(path);
            } else {
                status = "404 Not Found";
                body = "<html><body><h1>404 Not Found</h1></body></html>";
            }
        }
        std::string conn=keepAlive?"keep-alive":"close";
        std::string response=
            "HTTP/1.1 "+status+"\r\n"
            "Content-Type: "+contentType+"\r\n"
            "Content-Length: "+std::to_string(body.size())+"\r\n"
            "Connection: "+conn+"\r\n"
            "\r\n"+body;
       send(fd_,response.c_str(),response.size(),0); 
       if(keepAlive){
        ep_->addFD(fd_,EPOLLIN);
       }
       else{
        close(fd_);
       }
            
    }
}

std::string HttpConn::parsePath(const std::string&request)const{
    size_t pos=request.find(" ");
    size_t pos2=request.find(" ",pos+1);
    if(pos==std::string::npos||pos2==std::string::npos){
        return "/";
    }
    return request.substr(pos+1,pos2-pos-1);
}

bool HttpConn::isKeepAlive(const std::string&request)const{
    return request.find("Connection: close")==std::string::npos;
}
std::string HttpConn::getContentType(const std::string& path) const {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css")  != std::string::npos) return "text/css";
    if (path.find(".txt")  != std::string::npos) return "text/plain";
    if (path.find(".png")  != std::string::npos) return "image/png";
    if (path.find(".jpg")  != std::string::npos) return "image/jpeg";
    return "application/octet-stream";   // 兜底
}