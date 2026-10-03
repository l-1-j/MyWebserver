#pragma once
#include<iostream>
#include<mutex>
#include<string>
#include<fstream>
#include<thread>
#include"block_queue.h"
#include<ctime>


class Log{
    public:
    enum Level{DEBUG=0,INFO=1,WARN=2,ERROR=3,FATAL=4};
    static Log&getInstance(){
        static Log instance;
        return instance;
    }
    void init(const std::string&filename){
        filename_=filename;
        file_.open(filename_,std::ios::app);
        writer_=std::thread(&Log::writeLoop,this);
    }
    void write(Level level,const std::string&msg){
        if(level<level_){
            return;
        }
        queue_.push(getTime()+" "+msg);
    }
    std::string getTime(){
        time_t now=time(NULL);
        struct tm t;
        localtime_r(&now,&t);
        char buf[64];
        strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&t);
        return std::string(buf);    
    }
    private:
    Log():level_(INFO),queue_(1024){}
    Log(const Log&)=delete;
    Log&operator=(const Log&)=delete;
    void writeLoop(){
        while(true){
            std::string msg=queue_.pop();
            file_<<msg<<std::endl;
            std::cout<<msg<<std::endl;
        }
    }

    Level level_;
    std::ofstream file_;
    std::string filename_;
    BlockQueue<std::string>queue_;
    std::thread writer_;

};