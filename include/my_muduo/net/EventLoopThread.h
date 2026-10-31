#pragma once

#include <my_muduo/base/noncopyable.h>
#include <my_muduo/base/Thread.h>

#include <mutex>
#include <condition_variable>
#include <string>

class EventLoop;

class EventLoopThread : public noncopyable
{
public:
    explicit EventLoopThread(const std::string &name = "");
    ~EventLoopThread();

    EventLoop *startLoop();

private:
    void threadFunc();

    EventLoop* loop_;
    Thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
};
