#pragma once
#include<string>
#include<map>
#include<mutex>
#include<random>

class SessionManager{
    public:
    static SessionManager&getInstance(){
        static SessionManager instance;
        return instance;
    }
    
    //登录成功：为username创建session，返回sessionid
    std::string createSession(const std::string&username);

    //根据sessionid查询用户名，找不到返回空字符串
    std::string getUsername(const std::string&sid);

    //登出时删除session
    void removeSession(const std::string&sid);
    
    private:
    SessionManager()=default;
    SessionManager(const SessionManager&)=delete;
    SessionManager&operator=(const SessionManager&)=delete;

    std::string generateId();//随机生成32为hax字符

    std::map<std::string,std::string>sessions_; //sid->username;
    std::mutex mtx_;
};