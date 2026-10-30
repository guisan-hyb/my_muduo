#include <my_muduo/net/TcpServer.h>
#include <my_muduo/base/Logger.h>
#include <my_muduo/net/TcpConnection.h>

#include <functional>
#include <cstring>

EventLoop* CheckLoopNotNull(EventLoop* loop){
    if(loop == nullptr){
        LOG_FATAL("%s:%s:%d mainLoop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }

    return loop;
}

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &nameArg, TcpServer::Option option)
    : loop_(CheckLoopNotNull(loop)), ipPort_(listenAddr.toIpPort()), name_(nameArg)
    , acceptor_(new Acceptor(loop,listenAddr, option == kReusePort)), threadPool_(new EventLoopThreadPool(loop,nameArg))
    , connectionCallback_(), messageCallback_(),nextConnId_(1), started_(0)
{
    // 当有新用户连接时，会执行TcpServer::newConnection回调
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, 
        std::placeholders::_1, std::placeholders::_2));
}

// 连接的销毁（析构）必须在它所属的IO线程中完成
// TcpConnection可能属于其他线程
TcpServer::~TcpServer(){
    for(auto& item : connections_){
        // 这个局部的shared_ptr，出右括号，可以自动释放new出来的TcpConnection对象资源了
        TcpConnectionPtr conn(item.second);// ① 拷贝构造，引用计数+1
        item.second.reset();// ② 原map中的指针置空，引用计数-1

        // 销毁连接
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));// ③ 把任务派发到连接自己的线程
        // ④ conn是局部变量，作用域结束，引用计数-1
    }
}

void TcpServer::setThreadNum(int numThreads){
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start(){
    if(started_++ == 0){ // 防止一个TcpServer对象被start多次
        threadPool_->start(threadInitCallback_);// 启动底层的loop线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

// 有一个新的客户端的连接，acceptor会执行这个回调
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr){
    // 轮询算法，选择一个subLoop，来管理channel
    EventLoop *ioLoop = threadPool_->getNextLoop();

    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.data(), nextConnId_++);
    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
             name_.data(), connName.data(), peerAddr.toIpPort().data());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    memset(&local, 0, sizeof local);
    socklen_t addrlen = sizeof local;
    if(getsockname(sockfd,(sockaddr*)&local,&addrlen) < 0){
        LOG_ERROR("socket::getsocketname getLocalAddr");
    }

    InetAddress localAddr(local);

    // 根据连接成功的sockfd, 创建TcpConnection连接对象
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
    connections_[connName] = conn;
    // 下面的回调是用户设置的(设置给TcpServer) TcpServer => TcpConnection => Channel => Poller -> notify channel
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // 直接调用TcpConnection::connectEstablished
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn){
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn){
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n", name_.data(), conn->name().data());

    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop();
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
