#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockQueue{
    public:
    explicit BlockQueue(int maxSize):maxSize_(maxSize),closed_(false){}
    //关闭队列，唤醒所有阻塞的push和pop，之后不再接收新元素
    void close(){
        std::unique_lock<std::mutex>lock(mtx_);
        closed_=true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }
    //入队，队伍关闭时返回false，否则返回true
    bool push(const T&item){
        std::unique_lock<std::mutex>lock(mtx_);
        notFull_.wait(lock,[this](){return closed_|| queue_.size()<(size_t)maxSize_;});
        if(closed_){
            return false;
        }
        queue_.push(item);
        notEmpty_.notify_one();
        return true;
    }
    bool pop(T&item){
        std::unique_lock<std::mutex>lock(mtx_);
        notEmpty_.wait(lock,[this](){return closed_|| !queue_.empty();});
        if(closed_&&queue_.empty()){
            return false;
        }
        item=queue_.front();
        queue_.pop();
        notFull_.notify_one();
        return true;
    }
    private:
    std::queue<T>queue_;
    std::mutex mtx_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    bool closed_;
    int maxSize_;
};
        
    
