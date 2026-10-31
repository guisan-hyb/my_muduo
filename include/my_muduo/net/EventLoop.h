#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <sys/types.h>
#include <memory>
#include <mutex>

#include <my_muduo/base/noncopyable.h>
#include <my_muduo/base/Timestamp.h>
#include <my_muduo/base/CurrentThread.h>

class Channel;
class Poller;

// 事件循环类：ChannelList + Poller(epoll的抽象)
class EventLoop : public noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop(); // 开启事件循环
    void quit(); // 退出事件循环

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    void runInLoop(Functor cb); // 在当前loop中执行cb
    void queueInLoop(Functor cb); // 把cb放入队列，唤醒loop所在的线程，执行cb

    void wakeup(); // 唤醒loop所在的线程

    // EventLoop的方法 => 调用Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);
    
    // 判断EventLoop对象是否在自己的线程里面
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
    void assertInLoopThread() const;

private:
    void handleRead(); // wake up
    void doPendingFunctors(); // 执行回调

    using ChannelList = std::vector<Channel *>;

    std::atomic<bool> looping_;
    std::atomic<bool> quit_; // 标识退出loop循环
    
    const pid_t threadId_; // 记录当前loop所在的线程id
    Timestamp pollReturnTime_;// poller返回发生事件的channels的时间点
    std::unique_ptr<Poller> poller_;

    // eventfd
    int wakeupFd_;// 主要作用: 当mainloop获取一个新用户的channel, 通过轮询算法选择一个subloop, 通过该成员唤醒subloop处理channel
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;

    std::atomic<bool> callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; // 存储loop需要执行的所有回调操作
    std::mutex mutex_; // 保护上面vector的线程安全操作
};
