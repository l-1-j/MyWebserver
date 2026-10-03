#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockQueue{
    public:
    explicit BlockQueue(int maxSize):maxSize_(maxSize){}
    void push(const T&item){
        std::unique_lock<std::mutex>lock(mtx_);
        notFull_.wait(lock,[this](){return queue_.size()<(size_t)maxSize_;});
        queue_.push(item);
        notEmpty_.notify_one();
    }
    T pop(){
        std::unique_lock<std::mutex>lock(mtx_);
        notEmpty_.wait(lock,[this](){return !queue_.empty();});
        T item=queue_.front();
        queue_.pop();
        notFull_.notify_one();
        return item;
    }
    private:
    std::queue<T>queue_;
    std::mutex mtx_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    
    int maxSize_;
};
        
    
