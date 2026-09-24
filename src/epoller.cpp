#include"epoller.h"
Epoller::Epoller(int maxevents){
    epfd_=epoll_create(1);
    events_.resize(maxevents);
}
Epoller::~Epoller(){
    close(epfd_);
}
bool Epoller::addFD(int fd,uint32_t events){
    epoll_event ev;
    ev.data.fd=fd;
    ev.events=events;
    return epoll_ctl(epfd_,EPOLL_CTL_ADD,fd,&ev)==0;
}
bool Epoller::deFD(int fd){
    return epoll_ctl(epfd_,EPOLL_CTL_DEL,fd,nullptr)==0;
}
int Epoller::wait(int timeout){
    return epoll_wait(epfd_,events_.data(),events_.size(),timeout);
}
int Epoller::getEventFD(int id)const{
    return events_[id].data.fd;
}