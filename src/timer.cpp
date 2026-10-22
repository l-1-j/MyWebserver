#include"timer.h"
#include<ctime>
TimerHeap&TimerHeap::getInstance(){
    static TimerHeap instance;
    return instance;
}
void TimerHeap::addTimer(int fd,time_t expire){
    std::lock_guard<std::mutex>lock(mtx_);
    int idx=find(fd);
    if(idx!=-1){
        //已存在，更新到期时间
        time_t old=heap_[idx].expire;
        heap_[idx].expire=expire;
        //到期时间晚了就下沉，早了就上浮
        if(expire>old){
            siftDown(idx);
        }
        if(expire<old){
            siftUp(idx);
        }
        return;
    }
    //不存在,先插入堆尾，再上浮到合适位置
    heap_.push_back({fd,expire});
    siftUp(heap_.size()-1);
}

void TimerHeap::delTimer(int fd){
    std::lock_guard<std::mutex>lock(mtx_);
    int idx=find(fd);
    if(idx==-1){
        return;
    }
    //把要删除的元素和堆尾交换，删掉堆尾，再调整位置
    swapNode(idx,heap_.size()-1);
    heap_.pop_back();
    if(idx<heap_.size()){
        siftDown(idx);
        siftUp(idx);
    }
}

time_t TimerHeap::topExpire()const{
    std::lock_guard<std::mutex>lock(mtx_);
    return heap_.empty()?-1:heap_[0].expire;
}

bool TimerHeap::empty()const{
    std::lock_guard<std::mutex>lock(mtx_);
    return heap_.empty();
}
//触发发所有超时连接
void TimerHeap::tick(std::vector<int>&expired){
    std::lock_guard<std::mutex>lock(mtx_);
    time_t now=time(NULL);
    while(!heap_.empty()&&heap_[0].expire<=now){
        expired.push_back(heap_[0].fd);
        swapNode(0,heap_.size()-1);
        heap_.pop_back();
        if(!heap_.empty()){
            siftDown(0);
        }
    }
}
//堆用数组存储，不是线性排列，但是堆顶元素时间最小
void TimerHeap::siftUp(size_t i){
    while(i>0){
        size_t parent=(i-1)/2;
        if(heap_[i].expire<heap_[parent].expire){
            swapNode(i,parent);
            i=parent;
        }
        else{
            break;
        }
    }
}

void TimerHeap::siftDown(size_t i){
    size_t n=heap_.size();
    while(true){
        size_t left=2*i+1;//i的左孩子
        size_t right=2*i+2;//i的右孩子
        size_t smallest=i;//假设自己最小
        if(left<n&&heap_[left].expire<heap_[smallest].expire){
            smallest=left;//左孩子更小，更新最小值
        }
        if(right<n&&heap_[right].expire<heap_[smallest].expire){
            smallest=right;//右孩子更小，覆盖
        }
        if(smallest==i){
            break;//自己是三者里面最小的，ok
        }
        swapNode(i,smallest);//下沉
        i=smallest;
        //重新在新位置继续上面流程，直到当前位置是比左右孩子小,确保根最小的

    }
}

void TimerHeap::swapNode(size_t i,size_t j){
    Node tmp=heap_[i];
    heap_[i]=heap_[j];
    heap_[j]=tmp;
}

int TimerHeap::find(int fd)const{
    for(size_t i=0;i<heap_.size();i++){
        if(heap_[i].fd==fd){
            return(int)i;
        }
    }
    return -1;
}