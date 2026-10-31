#pragma once

#include <my_muduo/base/noncopyable.h>
#include <my_muduo/net/InetAddress.h>
#include <my_muduo/net/Callbacks.h>
#include <my_muduo/net/Buffer.h>
#include <my_muduo/base/Timestamp.h>

#include <memory>
#include <string>
#include <string_view>
#include <atomic>
#include <utility>

class Channel;
class EventLoop;
class Socket;
class TcpServer;

/*
    TcpServer => Acceptor => 有一个新用户连接，通过accept函数拿到connfd
    => TcpConnection 设置回调 => Channel => Poller => Channel回调操作
*/
class TcpConnection : public noncopyable, public std::enable_shared_from_this<TcpConnection>
{
    enum class StateE {Disconnected, Connecting, Connected, Disconnecting};
public:
    TcpConnection(EventLoop *loop, TcpServer &server, const std::string &name,
                  int sockfd, const InetAddress &localAddr,
                  const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &name() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }

    bool connected() const { return state_.load() == StateE::Connected; }

    // 发送数据
    void send(std::string data);
    // 关闭当前连接
    void shutdown();

private:
    friend class TcpServer;

    void setConnectionCallback(ConnectionCallback cb) {
        connectionCallback_ = std::move(cb);
    }
    void setMessageCallback(MessageCallback cb) {
        messageCallback_ = std::move(cb);
    }
    void setWriteCompleteCallback(WriteCompleteCallback cb) {
        writeCompleteCallback_ = std::move(cb);
    }

    void setState(StateE state) { state_ = state; }

    void connectEstablished();
    void connectDestroyed();

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(std::string_view data);
    void shutdownInLoop();
    void closeInLoop();

    EventLoop *loop_; // 这里绝对不是baseloop, 因为TcpConnection都是在subLoop里面管理的
    TcpServer &server_;
    const std::string name_;
    std::atomic<StateE> state_;

    // 类似Acceptor, Acceptor => mainLoop ; TcpConnection => subLoop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;// 有新连接时的回调
    MessageCallback messageCallback_;// 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;// 消息发送完成以后的回调

    Buffer inputBuffer_;
    Buffer outputBuffer_;
};

