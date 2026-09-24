#pragma once
#include<sys/epoll.h>
#include<vector>
#include<unistd.h>
class Epoller{
    public:
    explicit Epoller(int maxEvents=1024);
    ~Epoller();
    bool addFD(int fd,uint32_t events);
    bool deFD(int fd);
    int wait(int timeout=-1);
    int getEventFD(int id)const;
    private:
    int epfd_;
    std::vector<epoll_event> events_;
    
    

};