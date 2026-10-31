#include <my_muduo/net/Acceptor.h>
#include <my_muduo/net/EventLoop.h>
#include <my_muduo/net/InetAddress.h>
#include <my_muduo/base/Logger.h>
#include <my_muduo/net/TcpServer.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

static int createNonblocking(){
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if(sockfd < 0){
        LOG_FATAL("%s:%s:%d listen socket create err: %d \n", __FILE__, __FUNCTION__, __LINE__, errno);
    }

    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport,
                   TcpServer &server)
    : loop_(loop)
    , server_(server)
    , acceptSocket_(createNonblocking())
    , acceptChannel_(loop, acceptSocket_.fd())
    , listening_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(reuseport);
    acceptSocket_.bindAddress(listenAddr);

    acceptChannel_.setReadCallback([this](Timestamp) {
        handleRead();
    });
}

Acceptor::~Acceptor(){
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen(){
    loop_->assertInLoopThread();
    listening_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading(); // acceptChannel_ => Poller
}

// listenfd有事件发生了，就是有新用户连接了
void Acceptor::handleRead(){
    loop_->assertInLoopThread();
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if(connfd >= 0){
        server_.newConnection(connfd, peerAddr);
    }else{
        LOG_ERROR("%s:%s:%d accpet err: %d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        if(errno == EMFILE){ // 文件句柄数不够 
            LOG_ERROR("%s:%s:%d sockfd reached limit! \n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}

