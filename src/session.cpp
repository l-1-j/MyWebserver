#include"session.h"

//生成32位随机hex字符作为session id
std::string SessionManager::generateId(){
    static std::mt19937 rng(std::random_device{}());
    static const char*hex="0123456789abcdef";
    std::uniform_int_distribution<int>dist(0,15);
    std::string id;
    for(int i=0;i<32;i++){
        id+=hex[dist(rng)];
    }
    return id;
}

std::string SessionManager::createSession(const std::string&username){
    std::string sid=generateId();
    std::lock_guard<std::mutex>lock(mtx_);
    sessions_[sid]=username;
    return sid;
}

std::string SessionManager::getUsername(const std::string&sid){
    std::lock_guard<std::mutex>lock(mtx_);
    auto it=sessions_.find(sid);
    if(it ==sessions_.end()){
        return"";
    }
    return it->second;
}

void SessionManager::removeSession(const std::string&sid){
    std::lock_guard<std::mutex>lock(mtx_);
    sessions_.erase(sid);
}