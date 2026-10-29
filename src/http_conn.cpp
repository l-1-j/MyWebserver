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
#include<fcntl.h>
#include"timer.h"
#include"session.h"

// 待关闭 fd 队列的实现（主线程 + worker 线程共享，用锁保护）
static std::queue<int> g_closeQueue;//专门用来接收worker线程里面的断开的连接
static std::mutex g_closeMtx;

void   addCloseFd(int fd){
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
    // fd 复用时会拿到旧的 HttpConn 对象，把残留状态清干净
    readBuf_.clear();
    writeBuf_.clear();
    writeIdx_=0;
    checkState_=CHECK_STATE_REQUESTLINE;
    checkIdx_=0;
    contentLength_=0;
    body_.clear();
    keepAlive_=true;
    //将fd修改为非阻塞
    int oldFlag=fcntl(fd,F_GETFL);
    fcntl(fd,F_SETFL,oldFlag|O_NONBLOCK);
}

// 从状态机：从 checkIdx_ 开始抠出一行（到 \r\n）
LINE_STATUS HttpConn::parseLine(){
    size_t pos=readBuf_.find("\r\n",checkIdx_);
    if(pos==std::string::npos){
        return LINE_OPEN;   // 还没凑齐一行（半包）
    }
    lineStart_=checkIdx_;
    lineEnd_=pos;
    checkIdx_=pos+2;        // 跳过 \r\n
    return LINE_OK;
}

// 主状态机：循环解析，直到请求完整 或 数据不够
HTTP_CODE HttpConn::processRead(){
    while(true){
        if(checkState_==CHECK_STATE_CONTENT){
            return parseContent();   // 请求体按字节判断，不走行
        }
        LINE_STATUS st=parseLine();
        if(st==LINE_OPEN){
            return NO_REQUEST;       // 行不完整，等下次 recv
        }
        std::string line=readBuf_.substr(lineStart_,lineEnd_-lineStart_);
        if(checkState_==CHECK_STATE_REQUESTLINE){
            if(parseRequestLine(line)==BAD_REQUEST) return BAD_REQUEST;
        }else if(checkState_==CHECK_STATE_HEADER){
            HTTP_CODE c=parseHeaders(line);
            if(c==BAD_REQUEST) return BAD_REQUEST;
            if(c==GET_REQUEST) return GET_REQUEST;
        }
    }
}

// 解析请求行："GET /hello HTTP/1.1"
HTTP_CODE HttpConn::parseRequestLine(const std::string& line){
    size_t p1=line.find(' ');
    if(p1==std::string::npos) return BAD_REQUEST;
    size_t p2=line.find(' ',p1+1);
    if(p2==std::string::npos) return BAD_REQUEST;
    method_=line.substr(0,p1);
    path_=line.substr(p1+1,p2-p1-1);
    checkState_=CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析请求头：逐行，遇到空行表示头结束
HTTP_CODE HttpConn::parseHeaders(const std::string& line){
    if(line.empty()){
        // 空行 = 请求头结束
        if(contentLength_>0){
            checkState_=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;   // 无 body（GET），请求完整
    }
    size_t pos=line.find("Content-Length:");
    if(pos!=std::string::npos){
        contentLength_=std::stoi(line.substr(pos+15));   // "Content-Length:" 是 15 字符
    }
    return NO_REQUEST;
}

// 解析请求体：检查 body 是否按 Content-Length 读齐了
HTTP_CODE HttpConn::parseContent(){
    if(readBuf_.size()-checkIdx_ >= (size_t)contentLength_){
        body_=readBuf_.substr(checkIdx_,contentLength_);
        checkIdx_+=contentLength_;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 非阻塞发送响应：能发多少发多少，返回是否全部发完
bool HttpConn::write(){
    while(writeIdx_ < writeBuf_.size()){
        ssize_t r=send(fd_, writeBuf_.c_str()+writeIdx_, writeBuf_.size()-writeIdx_, 0);
        if(r>0){
            writeIdx_+=r;
            lastActive_=time(NULL);
        }
        else if(r<0){
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                return false;   // 发送缓冲满，没写完，等 EPOLLOUT 再续写
            }
            if(errno==EINTR){
                continue;       // 被信号打断，重试
            }
            // 其他错误（含 EPIPE，SIGPIPE 已全局忽略）：关闭连接
            if(fd_!=-1) addCloseFd(fd_);
            return false;
        }
        else{//r==0 对端关闭
            if(fd_!=-1) addCloseFd(fd_);
            return false;
        }
    }
    writeBuf_.clear();
    writeIdx_=0;
    return true;
}

void HttpConn::process(){
    // 上次响应还没写完（EPOLLOUT 触发），先继续写完
    if(!writeBuf_.empty()){
        if(!write()){
            // 还是没写完，继续挂 EPOLLOUT 等可写
            if(!ep_->modFD(fd_,EPOLLOUT|EPOLLET|EPOLLONESHOT)){
                Log::getInstance().write(Log::ERROR, "modFD失败 fd=" + std::to_string(fd_) + ": " + std::string(strerror(errno)));
            }
            TimerHeap::getInstance().addTimer(fd_,time(NULL)+TIMEOUT);
            return;
        }
        // 写完了。若这个响应要求关闭连接，写完直接关
        if(!keepAlive_){
            if(fd_!=-1) addCloseFd(fd_);
            return;
        }
        // 否则继续往下：读新数据 + 处理缓冲里可能残留的粘包请求
    }

    char buff[1024]={0};
    while(true){
        int r=recv(fd_,buff,sizeof(buff),0);
        if(r>0){
            readBuf_.append(buff,r);
            lastActive_=time(NULL);
            continue;
        }
        else if(r==0){
            //客户端断开连接
            if(fd_!=-1)addCloseFd(fd_);
            return;
        }
        else{//r<0
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                break;//读完了开始处理
            }
            if(errno==EINTR){
                continue;//被信号打断重试
            }
            Log::getInstance().write(Log::ERROR, "recv失败 fd=" + std::to_string(fd_) + ": " + std::string(strerror(errno)));
            if(fd_!=-1) addCloseFd(fd_);
            return;
        }
    }

    // 循环处理缓冲里的完整请求：一次 recv 可能粘了多个请求（粘包）
    while(true){
        // 状态机解析
        HTTP_CODE code=processRead();
        if(code==NO_REQUEST){
            // 半包：数据没读完，重新挂上事件，等下次触发继续读
            if(!ep_->modFD(fd_,EPOLLIN|EPOLLET|EPOLLONESHOT)){
                Log::getInstance().write(Log::ERROR, "modFD失败 fd=" + std::to_string(fd_) + ": " + std::string(strerror(errno)));
            }
            TimerHeap::getInstance().addTimer(fd_,time(NULL)+TIMEOUT);
            return;
        }
        if(code==BAD_REQUEST||code==INTERNAL_ERROR){
            if(fd_!=-1) addCloseFd(fd_);
            return;
        }

        Log::getInstance().write(Log::INFO, "客户端" + ip_ + "请求" + path_);
        std::string body;//请求体
        std::string status="200 OK";
        std::string contentType="text/html";   // 默认类型
        std::string extraHeaders;//存放额外响应头

        if(path_=="/") {
            body="<html><body><h1>Welcome to the Home Page!</h1></body></html>";
        }
        else if(path_=="/hello"){
            body="<html><body><h1>Hello, World!</h1></body></html>";
        }
        //注册
        else if(path_=="/register"){
            std::string username = parseParam(body_, "username");
            std::string passwd = parseParam(body_, "passwd");
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
            //sql预处理，防止sql注入
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
        else if(path_=="/login"){
            std::string username = parseParam(body_, "username");
            std::string passwd = parseParam(body_, "passwd");

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
                    //密码正确，建session，发cookie，302跳转到欢迎页面
                    std::string sid=SessionManager::getInstance().createSession(username);
                    status="302 Found";
                    extraHeaders += "Set-Cookie: sid=" + sid + "; Path=/\r\n";
                    extraHeaders += "Location: /welcome\r\n";
                    body = "";   // 302 重定向不需要 body
                    
                }else{
                    body="密码错误";
                }
            }
            mysql_stmt_close(stmt);
            }

        }
        else if(path_=="/welcome"){
            std::string sid=parseCookie(readBuf_);
            std::string username=SessionManager::getInstance().getUsername(sid);
            if(username.empty()){
                //没登录成就session失败，返回首页
                status="302 Found";
                extraHeaders+="Location: /\r\n";
                body="";
            }else{
                body="<html><body><h1>欢迎，" + username + "！</h1></body></html>";
            }
        }
        else{
            // 尝试当静态文件：www/ + 路径
            std::ifstream file("www" + path_, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                body = ss.str();
                contentType = getContentType(path_);
            } else {
                status = "404 Not Found";
                body = "<html><body><h1>404 Not Found</h1></body></html>";
            }
        }

        // 只看当前这个请求的头部（checkIdx_ 之前），别把缓冲里下一个请求的 Connection 也算进来
        keepAlive_=isKeepAlive(readBuf_.substr(0, checkIdx_));
        std::string conn=keepAlive_?"keep-alive":"close";
        std::string response=
            "HTTP/1.1 "+status+"\r\n"
            "Content-Type: "+contentType+"\r\n"
            "Content-Length: "+std::to_string(body.size())+"\r\n"
            "Connection: "+conn+"\r\n"
            + extraHeaders +
            "\r\n"+body;

        // 先消费掉这一个请求（保留缓冲里剩余的字节），再发响应。
        // 这样即使 send 没发完（EAGAIN）提前返回，状态也已重置，续写完能正确解析下一个粘包请求
        readBuf_.erase(0, checkIdx_);
        checkState_=CHECK_STATE_REQUESTLINE;
        checkIdx_=0;
        contentLength_=0;
        body_.clear();

        // 非阻塞发送：没发完的部分留在 writeBuf_，挂 EPOLLOUT 续写
        writeBuf_=response;
        writeIdx_=0;
        if(!write()){
            if(!ep_->modFD(fd_,EPOLLOUT|EPOLLET|EPOLLONESHOT)){
                Log::getInstance().write(Log::ERROR, "modFD失败 fd=" + std::to_string(fd_) + ": " + std::string(strerror(errno)));
            }
            TimerHeap::getInstance().addTimer(fd_,time(NULL)+TIMEOUT);
            return;
        }

        // 本请求要求关闭连接，或缓冲里没有剩余请求了，就退出循环
        if(!keepAlive_ || readBuf_.empty()){
            break;
        }
    }

    if(keepAlive_){
        if(!ep_->modFD(fd_,EPOLLIN|EPOLLET|EPOLLONESHOT)){
            Log::getInstance().write(Log::ERROR, "modFD失败 fd=" + std::to_string(fd_) + ": " + std::string(strerror(errno)));
        }
        TimerHeap::getInstance().addTimer(fd_,time(NULL)+TIMEOUT);
    }
    else{
        // 不在这里 close（会和主线程 accept 竞争 fd），交给主线程统一关闭
        if(fd_!=-1){
            addCloseFd(fd_);
        }
    }
}

std::string HttpConn::parseCookie(const std::string&request)const{
    std::string key="Cookie: ";
    size_t pos=request.find(key);
    if(pos==std::string::npos){
        return"";
    }
    pos+=key.size();
    size_t end=request.find("\r\n",pos);
    std::string line=request.substr(pos,end-pos);

    size_t eq=line.find("sid=");
    if(eq==std::string::npos){
        return"";
    }
    eq+=4;
    size_t semi=line.find(";",eq);
    if(semi==std::string::npos){
        semi=line.size();
    }
    return line.substr(eq,semi-eq);
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
