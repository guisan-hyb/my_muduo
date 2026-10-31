#pragma once

#include <my_muduo/net/EventLoop.h>
#include <my_muduo/net/Acceptor.h>
#include <my_muduo/net/InetAddress.h>
#include <my_muduo/base/noncopyable.h>
#include <my_muduo/net/EventLoopThreadPool.h>
#include <my_muduo/net/Callbacks.h>

#include <my_muduo/net/TcpConnection.h>
#include <my_muduo/net/Buffer.h>

#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <utility>

// 对外的服务器编程使用的类
class TcpServer : public noncopyable
{
public:
    enum Option{
        kNoReusePort,
        kReusePort
    };

    TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &nameArg, Option option = kNoReusePort);
    ~TcpServer();

    void setConnectionCallback(ConnectionCallback cb) {
        connectionCallback_ = std::move(cb);
    }
    void setMessageCallback(MessageCallback cb) {
        messageCallback_ = std::move(cb);
    }
    void setWriteCompleteCallback(WriteCompleteCallback cb) {
        writeCompleteCallback_ = std::move(cb);
    }

    // 设置底层subloop的个数
    void setThreadNum(int numThreads);

    // 开启服务器监听
    void start();

private:
    friend class Acceptor;
    friend class TcpConnection;

    void newConnection(int sockfd, const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn);
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_;// baseloop 用户定义的loop

    const std::string ipPort_;
    const std::string name_;

    std::unique_ptr<Acceptor> acceptor_;// 运行在mainloop, 监听新连接
    std::unique_ptr<EventLoopThreadPool> threadPool_;

    ConnectionCallback connectionCallback_;// 有新连接时的回调
    MessageCallback messageCallback_;// 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;// 消息发送完成以后的回调

    std::atomic<int> started_;

    int nextConnId_;
    ConnectionMap connections_;
};
