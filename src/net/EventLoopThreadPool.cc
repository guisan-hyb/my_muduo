#include <my_muduo/net/EventLoopThreadPool.h>
#include <my_muduo/net/EventLoopThread.h>

#include <memory>
#include <utility>

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop), name_(nameArg), started_(false), numThreads_(0), next_(0)
{}


EventLoopThreadPool::~EventLoopThreadPool()
{}


void EventLoopThreadPool::start(){
    started_ = true;

    for (int i = 0; i < numThreads_;++i){
        const std::string threadName = name_ + std::to_string(i);
        auto thread = std::make_unique<EventLoopThread>(threadName);
        loops_.emplace_back(thread->startLoop());
        threads_.emplace_back(std::move(thread));
    }

}

EventLoop *EventLoopThreadPool::getNextLoop(){
    EventLoop *loop = baseLoop_;

    if(!loops_.empty()){
        loop = loops_[(next_++) % loops_.size()];
    }

    return loop;
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops(){
    if(loops_.empty()){
        return std::vector<EventLoop *>(1, baseLoop_);
    }else{
        return loops_;
    }
}

