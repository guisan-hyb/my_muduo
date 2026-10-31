#include <my_muduo/net/TcpServer.h>
#include <my_muduo/base/Logger.h>
#include <my_muduo/net/TcpConnection.h>

#include <sys/socket.h>
#include <unistd.h>
#include <utility>

EventLoop* CheckLoopNotNull(EventLoop* loop){
    if(loop == nullptr){
        LOG_FATAL("%s:%s:%d mainLoop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }

    return loop;
}

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &nameArg, TcpServer::Option option)
    : loop_(CheckLoopNotNull(loop)), ipPort_(listenAddr.toIpPort()), name_(nameArg)
    , acceptor_(std::make_unique<Acceptor>(
          loop, listenAddr, option == kReusePort, *this))
    , threadPool_(std::make_unique<EventLoopThreadPool>(loop, nameArg))
    , connectionCallback_(), messageCallback_(), started_(0), nextConnId_(1)
{}

// 连接的销毁（析构）必须在它所属的IO线程中完成
// TcpConnection可能属于其他线程
TcpServer::~TcpServer(){
    auto connections = std::move(connections_);
    for (auto& item : connections) {
        auto conn = std::move(item.second);
        conn->getLoop()->runInLoop([conn] {
            conn->connectDestroyed();
        });
    }
}

void TcpServer::setThreadNum(int numThreads){
    if (started_.load() != 0) {
        LOG_FATAL("%s", "TcpServer::setThreadNum must be called before start()\n");
    }
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start(){
    if(started_++ == 0){ // 防止一个TcpServer对象被start多次
        threadPool_->start();
        loop_->runInLoop([this] {
            acceptor_->listen();
        });
    }
}

// 有一个新的客户端的连接，acceptor会执行这个回调
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr){
    // 轮询算法，选择一个subLoop，来管理channel
    EventLoop *ioLoop = threadPool_->getNextLoop();

    std::string connName = name_ + "-" + ipPort_ + "#" +
                           std::to_string(nextConnId_++);

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
             name_.data(), connName.data(), peerAddr.toIpPort().data());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local{};
    socklen_t addrlen = sizeof local;
    if(getsockname(sockfd, reinterpret_cast<sockaddr *>(&local), &addrlen) < 0){
        LOG_ERROR("%s", "socket::getsocketname getLocalAddr");
        ::close(sockfd);
        return;
    }

    InetAddress localAddr(local);

    // 根据连接成功的sockfd, 创建TcpConnection连接对象
    auto conn = std::make_shared<TcpConnection>(
        ioLoop, *this, connName, sockfd, localAddr, peerAddr);
    connections_[connName] = conn;
    // 下面的回调是用户设置的(设置给TcpServer) TcpServer => TcpConnection => Channel => Poller -> notify channel
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    ioLoop->runInLoop([conn] {
        conn->connectEstablished();
    });
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn){
    loop_->runInLoop([this, conn] {
        removeConnectionInLoop(conn);
    });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn){
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n", name_.data(), conn->name().data());

    connections_.erase(conn->name());
    EventLoop *ioLoop = conn->getLoop();
    ioLoop->queueInLoop([conn] {
        conn->connectDestroyed();
    });
}
