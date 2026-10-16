#include"http_conn.h"
#include"epoller.h"
#include"sql_connection_pool.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include"log.h"
#include <openssl/evp.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <queue>
#include <mutex>

// 待关闭 fd 队列的实现（主线程 + worker 线程共享，用锁保护）
static std::queue<int> g_closeQueue;
static std::mutex g_closeMtx;

void addCloseFd(int fd){
    std::lock_guard<std::mutex> lock(g_closeMtx);
    g_closeQueue.push(fd);
}

bool getCloseFd(int& fd){
    std::lock_guard<std::mutex> lock(g_closeMtx);
    if(g_closeQueue.empty()){
        return false;
    }
    fd = g_closeQueue.front();
    g_closeQueue.pop();
    return true;
}

std::string HttpConn::sha256(const std::string&str){
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashlen=0;
    EVP_MD_CTX*ctx=EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx,EVP_sha256(),nullptr);
    EVP_DigestUpdate(ctx,str.c_str(),str.size());
    EVP_DigestFinal_ex(ctx,hash,&hashlen);
    EVP_MD_CTX_free(ctx);

    char buf[EVP_MAX_MD_SIZE*2+1];
    for(unsigned int i=0;i<hashlen;i++){
        std::sprintf(buf+i*2,"%02x",hash[i]);
    }
    buf[hashlen*2]='\0';
    return std::string(buf);

}
void HttpConn::init(int fd,Epoller*ep){
    fd_=fd;
    ep_=ep;
    lastActive_=time(NULL);
}

void HttpConn::process(){
    char buff[1024]={0};
    int r=recv(fd_,buff,sizeof(buff),0);
    if(r<=0){
        if(r<0){
            Log::getInstance().write(Log::ERROR, "recv失败 fd=" + std::to_string(fd_) + " r=" + std::to_string(r) + ": " + std::string(strerror(errno)));
        }
        // 不在这里 close（会和主线程 accept 竞争 fd），交给主线程统一关闭
        if(fd_!=-1){
            addCloseFd(fd_);
        }
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
        //注册
        else if(path=="/register"){
            std::string reqBody = parseBody(msg);
            std::string username = parseParam(reqBody, "username");
            std::string passwd = parseParam(reqBody, "passwd");
            std::string hashedPasswd = sha256(passwd);
              
            MYSQL* conn = nullptr;
            connectionRAII raii(&conn, connection_pool::getInstance());
           
            //查重
            MYSQL_STMT*stmt=mysql_stmt_init(conn);
            if(stmt==nullptr){
                Log::getInstance().write(Log::ERROR, "注册：连接初始化失败");
                body="服务器错误";
            }else{
            if(mysql_stmt_prepare(stmt,"SELECT username FROM users WHERE username=?",strlen("SELECT username FROM users WHERE username=?"))!=0){
                Log::getInstance().write(Log::ERROR, "注册：SQL预处理失败");
                body="服务器错误";
            }else{
            MYSQL_BIND bindsql;
            memset(&bindsql,0,sizeof(bindsql));//初始化清零
            bindsql.buffer_type=MYSQL_TYPE_STRING;//绑定类型字符串
            bindsql.buffer=(char*)username.c_str();//绑定数据
            bindsql.buffer_length=username.size();
            mysql_stmt_bind_param(stmt,&bindsql);
            
            mysql_stmt_execute(stmt);
            mysql_stmt_store_result(stmt);
            int cnt=mysql_stmt_num_rows(stmt);
            mysql_stmt_free_result(stmt);
            if(cnt>0){
                body="用户已存在";
            }else{
                //插入新用户
                MYSQL_STMT*stmt2=mysql_stmt_init(conn);
                mysql_stmt_prepare(stmt2,"INSERT INTO users(username,passwd) VALUES(?,?)",strlen("INSERT INTO users(username,passwd) VALUES(?,?)"));
                MYSQL_BIND bindins[2];
                memset(bindins,0,sizeof(bindins));
                bindins[0].buffer_type=MYSQL_TYPE_STRING;
                bindins[0].buffer=(char*)username.c_str();
                bindins[0].buffer_length=username.size();
                bindins[1].buffer_type=MYSQL_TYPE_STRING;
                bindins[1].buffer=(char*)hashedPasswd.c_str();
                bindins[1].buffer_length=hashedPasswd.size();
                mysql_stmt_bind_param(stmt2,bindins);
                
                if(mysql_stmt_execute(stmt2)==0){
                    body="注册成功";
                }else{
                    Log::getInstance().write(Log::ERROR, "注册：插入用户失败");
                    body="服务器错误";
                }
                mysql_stmt_close(stmt2);

            }
            mysql_stmt_close(stmt);
            }
            }
        }
        // 登录
        else if(path=="/login"){
            std::string reqBody = parseBody(msg);
            std::string username = parseParam(reqBody, "username");
            std::string passwd = parseParam(reqBody, "passwd");

            MYSQL* conn = nullptr;
            connectionRAII raii(&conn, connection_pool::getInstance());
            
            MYSQL_STMT*stmt=mysql_stmt_init(conn);
            if(stmt==nullptr || mysql_stmt_prepare(stmt,"SELECT passwd FROM users WHERE username=?",strlen("SELECT passwd FROM users WHERE username=?"))!=0){
                Log::getInstance().write(Log::ERROR, "登录：SQL预处理失败");
                body="服务器错误";
            }else{
            //绑定输入
            MYSQL_BIND bindUser;
            memset(&bindUser,0,sizeof(bindUser));
            bindUser.buffer_type=MYSQL_TYPE_STRING;
            bindUser.buffer=(char*)username.c_str();
            bindUser.buffer_length=username.size();
            mysql_stmt_bind_param(stmt,&bindUser);

            mysql_stmt_execute(stmt);
            //绑定输出
            char passwdbuf[65]={0};
            unsigned long passwdlen=0;
            MYSQL_BIND bindResult;
            memset(&bindResult,0,sizeof(bindResult));
            bindResult.buffer_type=MYSQL_TYPE_STRING;
            bindResult.buffer=passwdbuf;
            bindResult.buffer_length=sizeof(passwdbuf);
            bindResult.length=&passwdlen;
            mysql_stmt_bind_result(stmt,&bindResult);

            int ret=mysql_stmt_fetch(stmt);
            if(ret!=0){
                body="用户不存在";
            }else{
                std::string dbHash(passwdbuf,passwdlen);
                if(dbHash==sha256(passwd)){
                    body="登录成功";
                }else{
                    body="密码错误";
                }
            }
            mysql_stmt_close(stmt);
            }

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
            // 不在这里 close（会和主线程 accept 竞争 fd），交给主线程统一关闭
            if(fd_!=-1){
                addCloseFd(fd_);
            }
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
