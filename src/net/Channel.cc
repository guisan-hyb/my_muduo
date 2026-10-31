#include <my_muduo/net/Channel.h>
#include <my_muduo/net/EventLoop.h>
#include <my_muduo/base/Logger.h>

#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    {}


Channel::~Channel(){

}

// 当改变channel所表示fd的events事件后，update负责在poller里面更改fd相应事件的epoll_ctl
// EventLoop => ChannelList + Poller
void Channel::update(){
    // 通过channel所属的EventLoop, 调用poller的相应方法, 注册fd的events事件
    loop_->updateChannel(this);
}

// 在channel所属的EventLoop中，把当前的channel删除掉
void Channel::remove(){
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime){
    LOG_INFO("channel handleEvent revents: %d\n", revents_);

    // EPOLLHUP这个事件不用专门向epoll注册, epoll本身就给所有sockfd注册过这个事件了
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)){
        if(closeCallback_){
            closeCallback_();
        }
    }

    if(revents_ & EPOLLERR){
        if(errorCallback_){
            errorCallback_();
        }
    }

    if(revents_ & (EPOLLIN | EPOLLPRI)){
        if(readCallback_){
            readCallback_(receiveTime);
        }
    }

    if(revents_ & EPOLLOUT){
        if(writeCallback_){
            writeCallback_();
        }
    }
}

