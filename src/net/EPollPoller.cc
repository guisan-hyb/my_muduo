#include "EPollPoller.h"
#include <my_muduo/base/Logger.h>
#include <my_muduo/net/Channel.h>

#include <errno.h>
#include <unistd.h>
#include <cstring>

const int kNew = -1;// 一个channel还没有添加到Poller当中   channel的成员index_ == -1
const int kAdded = 1;// 一个channel已经添加到了Poller当中
const int kDeleted = 2;// 一个channel已经被Poller删除了

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(epoll_create1(0))
    , events_(kInitEventListSize)
{
    if(epollfd_ < 0){
        LOG_FATAL("epoll_create error: %d\n", errno);
    }
}

EPollPoller::~EPollPoller() {
    close(epollfd_);
}


Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels){
    // 实际上用LOG_DEBUG输出日志更为合理
    LOG_INFO("func = %s => fd total count: %lu\n", __FUNCTION__, channels_.size());

    int numEvents = epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs); // 第二个参数也可以用: &*events_.begin()
    int saveErrno = errno;
    Timestamp now(Timestamp::now());

    if(numEvents > 0){
        LOG_INFO("%d events happened\n", numEvents);
        fillActiveChannels(numEvents, activeChannels);

        if(numEvents == events_.size()){
            events_.resize(events_.size() * 2);
        }
    }else if(numEvents == 0){
        LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    }else{
        if(saveErrno != EINTR){
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }

    return now;
}

// channel update/remove => Eventloop updateChannel/removeChannel => Poller
/*
              EventLoop => poller.poll
    ChannelList         Poller
                    ChannelMap: <fd, Channel*>
*/
void EPollPoller::updateChannel(Channel *channel){
    const int index = channel->index();
    LOG_INFO("func = %s, fd = %d, events = %d, index = %d\n", __FUNCTION__, channel->fd(), channel->events(), index);

    if(index == kNew || index == kDeleted){
        if(index == kNew){
            int fd = channel->fd();
            channels_[fd] = channel;
        }

        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);
    }else{ // channel已经在poller上注册过了
        int fd = channel->fd();
        if(channel->isNoneEvent()){
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }else{
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 从poller中删除channel
void EPollPoller::removeChannel(Channel *channel){
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func = %s, fd = %d\n", __FUNCTION__, fd);

    int index = channel->index();
    if(index == kAdded){
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew);
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const {
    for (int i = 0; i < numEvents;++i){
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel);
    }
}

void EPollPoller::update(int operation, Channel *channel) {
    int fd = channel->fd();
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();
    // event.data.fd = fd;
    event.data.ptr = channel;

    if(epoll_ctl(epollfd_,operation,fd,&event) < 0){
        if(operation == EPOLL_CTL_DEL){
            LOG_ERROR("epoll_ctl del error: %d\n", errno);
        }else{
            LOG_FATAL("epoll_ctl add/mod error: %d\n", errno);
        }
    }
}