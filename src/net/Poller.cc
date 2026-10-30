#include <my_muduo/net/Poller.h>
#include <my_muduo/net/Channel.h>

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
    {}

bool Poller::hasChannel(Channel *channel) const{
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

