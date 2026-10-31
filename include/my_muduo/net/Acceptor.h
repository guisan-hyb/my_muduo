#pragma once

#include <my_muduo/base/noncopyable.h>
#include <my_muduo/net/Socket.h>
#include <my_muduo/net/Channel.h>

class EventLoop;
class InetAddress;
class TcpServer;

class Acceptor : public noncopyable
{
public:
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport,
             TcpServer &server);
    ~Acceptor();

    bool listening() const { return listening_; }
    void listen();

private:
    void handleRead();

    EventLoop *loop_; // Acceptor用的就是用户定义的那个baseloop, 也称作mainloop
    TcpServer &server_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    bool listening_;
};
