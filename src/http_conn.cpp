#include"http_conn.h"
#include"epoller.h"
#include"sql_connection_pool.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include"log.h"

void HttpConn::init(int fd,Epoller*ep){
    fd_=fd;
    ep_=ep;
    lastActive_=time(NULL);
}

void HttpConn::process(){
    char buff[1024]={0};
    int r=recv(fd_,buff,sizeof(buff),0);
    if(r<=0){
        closeConn();
        return;
    }
    else{
        lastActive_=time(NULL);
        std::string msg(buff);
        bool keepAlive=isKeepAlive(msg);
        std::string path=parsePath(msg);
        Log::getInstance().write(Log::INFO, "客户端" + ip_ + "请求" + path);
        std::string body;
        std::string status="200 OK";
        std::string contentType="text/html";   // 默认类型

        if(path=="/") {
            body="<html><body><h1>Welcome to the Home Page!</h1></body></html>";
        }
        else if(path=="/hello"){
            body="<html><body><h1>Hello, World!</h1></body></html>";
        }
        else if(path=="/register"){
            std::string reqBody = parseBody(msg);
            std::string username = parseParam(reqBody, "username");
            std::string passwd = parseParam(reqBody, "passwd");

            MYSQL* conn = nullptr;
            connectionRAII raii(&conn, connection_pool::getInstance());

            // ① 先查：用户名已存在吗？
            std::string checkSql = "SELECT username FROM users WHERE username='" + username + "'";
            mysql_query(conn, checkSql.c_str());
            MYSQL_RES* res = mysql_store_result(conn);
            if (mysql_num_rows(res) > 0) {
                body = "用户名已存在";
            } else {
                // ② 不存在，插入
                std::string insertSql = "INSERT INTO users(username, passwd) VALUES('" + username + "','" + passwd + "')";
                if (mysql_query(conn, insertSql.c_str()) == 0) {
                    body = "注册成功";
                } else {
                    body = "注册失败";
                }
            }
            mysql_free_result(res);
        }
        else if(path=="/login"){
            std::string reqBody = parseBody(msg);
            std::string username = parseParam(reqBody, "username");
            std::string passwd = parseParam(reqBody, "passwd");

            MYSQL* conn = nullptr;
            connectionRAII raii(&conn, connection_pool::getInstance());

            std::string sql = "SELECT passwd FROM users WHERE username='" + username + "'";
            mysql_query(conn, sql.c_str());
            MYSQL_RES* res = mysql_store_result(conn);
            if (mysql_num_rows(res) == 0) {
                body = "用户不存在";
            } else {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (passwd == row[0]) {
                    body = "登录成功";
                } else {
                    body = "密码错误";
                }
            }
            mysql_free_result(res);
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
            closeConn();
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

std::string HttpConn::parseBody(const std::string&request)const{
    size_t pos=request.find("\r\n\r\n");
    if(pos==std::string::npos){
        return "";
    }
    return request.substr(pos+4);
}

std::string HttpConn::parseParam(const std::string& body, const std::string& key) const {
    std::string target = key + "=";
    size_t pos = body.find(target);
    if (pos == std::string::npos) return "";
    pos += target.size();
    size_t end = body.find("&", pos);
    if (end == std::string::npos) end = body.size();
    return body.substr(pos, end - pos);
}

bool HttpConn::isKeepAlive(const std::string&request)const{
    // HTTP/1.1：默认 keep-alive，除非显式 Connection: close
    if(request.find("HTTP/1.1")!=std::string::npos){
        return request.find("Connection: close")==std::string::npos;
    }
    // HTTP/1.0：默认 close，除非显式 Connection: keep-alive
    return request.find("Connection: keep-alive")!=std::string::npos;
}

std::string HttpConn::getContentType(const std::string& path) const {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css")  != std::string::npos) return "text/css";
    if (path.find(".txt")  != std::string::npos) return "text/plain";
    if (path.find(".png")  != std::string::npos) return "image/png";
    if (path.find(".jpg")  != std::string::npos) return "image/jpeg";
    return "application/octet-stream";
}

void HttpConn::closeConn(){
    if(fd_!=-1){
        close(fd_);
        fd_=-1;
    }
}
