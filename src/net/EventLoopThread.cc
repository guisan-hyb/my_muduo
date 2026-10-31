#include <my_muduo/net/EventLoopThread.h>

#include <my_muduo/net/EventLoop.h>

EventLoopThread::EventLoopThread(const std::string &name)
    : loop_(nullptr)
    , thread_([this] {
        threadFunc();
    }, name)
{
    
}

EventLoopThread::~EventLoopThread(){
    EventLoop *loop = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop = loop_;
    }

    if(loop != nullptr){
        loop->quit();
        thread_.join();
    }
}

EventLoop *EventLoopThread::startLoop(){
    thread_.start(); // 启动新线程

    EventLoop *loop = nullptr;
    std::unique_lock<std::mutex> ulk(mutex_);
    cond_.wait(ulk, [&]() {
        return loop_ != nullptr;
    });

    loop = loop_;

    return loop;
}

void EventLoopThread::threadFunc(){
    EventLoop loop;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    loop.loop();

    std::lock_guard<std::mutex> lk(mutex_);
    loop_ = nullptr;
}
