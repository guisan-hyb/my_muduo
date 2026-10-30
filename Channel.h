#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;

// Channel 理解为通道，封装了sockfd和其感兴趣的event, 如: EPOLLIN, EPOLLOUT事件
// 还绑定了poller返回的具体事件
class Channel : public noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // fd得到poller通知以后，处理事件的 -> 调用相应的回调方法
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉, channel还在执行回调操作
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }
    
    // 设置fd相应的事件状态
    void enableReading() { events_ |= kReadEvent; update();}
    void disableReading() { events_ &= ~kReadEvent; update();}
    void enableWriting() { events_ |= kWriteEvent; update();}
    void disableWriting() { events_ &= ~kWriteEvent; update();}
    void disableAll() { events_ = kNoneEvent; update();}

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return (events_ & kWriteEvent) != 0; }
    bool isReading() const { return (events_ & kReadEvent) != 0; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    EventLoop *ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    // 表示当前fd的状态
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_;// 事件循环
    const int fd_;// fd, poller监听的对象
    int events_;// 注册fd感兴趣的事件
    int revents_;// poller返回的具体发生的事件
    int index_;// 表示channel在poller中的状态： 未添加/已添加/已删除

    // 防止手动remove channel后，还在使用channel
    // 所以进行了一个跨进程的对象生存状态的监听
    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为channel通道里面能够获知fd最终发生的具体的事件revents, 所以它负责调用具体事件的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};
