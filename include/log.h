#pragma once
#include<iostream>
#include<mutex>
#include<string>

class Log{
    public:
    static void info(const std::string&msg){
        std::lock_guard<std::mutex>lock(mtx_);
        std::cout<<msg<<std::endl;
    }
    private:
    inline static std::mutex mtx_;
};