#pragma once
#include<vector>
#include<ctime>
#include<mutex>
#include<unordered_map>
inline constexpr int TIMEOUT=5;
// 小根堆定时器：管理所有连接的「空闲到期时间」
// 堆顶永远是「最早到期」的连接，epoll_wait 的超时时间就是它
// 主线程(tick/add/del) 和 worker线程(keep-alive重新计时) 都会访问，所以用锁保护
class TimerHeap {
    public:
    static TimerHeap&getInstance();// 单例模式
    //设置fd到期时间，不存在则插入，存在则更新调整堆
    void addTimer(int fd, time_t expire);
    //删除fd的定时器，连接交给worker处理，
    void delTimer(int fd);
    //堆顶到期时间，堆空返回-1；
    time_t topExpire()const;
    bool empty()const;
    //弹出所以到期fd，塞进expir,由调用方负责关闭连接
    void tick(std::vector<int>&expir);
    private:
    TimerHeap()=default;
    TimerHeap(const TimerHeap&)=delete;
    TimerHeap& operator=(const TimerHeap&)=delete;
    struct Node{
        int fd;//关联的链接
        time_t expire;//到期时间
    };
    std::vector<Node>heap_;
    mutable std::mutex mtx_;

    void siftUp(size_t i);//上浮；时间短的往上走
    void siftDown(size_t i);//下沉，时间长的往下走
    void swapNode(size_t i,size_t j);//交换两个位置
    int find(int fd)const;//线性查找fd下标，找不到返回-1；

};