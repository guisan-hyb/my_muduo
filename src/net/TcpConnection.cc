#include <my_muduo/net/TcpConnection.h>

#include <my_muduo/base/Logger.h>
#include <my_muduo/net/Socket.h>
#include <my_muduo/net/Channel.h>
#include <my_muduo/net/EventLoop.h>

#include <unistd.h>
#include <errno.h>
#include <functional>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>


static EventLoop* CheckLoopNotNull(EventLoop* loop){
    if(loop == nullptr){
        LOG_FATAL("%s:%s:%d subloop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop)), name_(name), state_(kConnecting), reading_(true)
    , socket_(new Socket(sockfd)), channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr),peerAddr_(peerAddr)
    ,highWaterMark_(64*1024*1024) // 64M
{
    // 给channel设置相应的回调函数, poller给channel通知感兴趣的事件发生了，channel会回调相应的操作函数
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[%s] at fd = %d\n", name_.data(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection(){
    LOG_INFO("TcpConnection::dtor[%s] at fd = %d state = %d \n", name_.data(), channel_->fd(), state_.load());
}

void TcpConnection::send(const std::string& buf){
    if(state_ == kConnected){
        if(loop_->isInLoopThread()){
            sendInLoop(buf.data(), buf.size());
        }else{
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf.data(), buf.size()));
        }
    }
}

// 发送数据  应用写的快，而内核发送数据慢，需要把带发送数据写入缓冲区，设置水位回调
void TcpConnection::sendInLoop(const void *data, size_t len){
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 之前调用过该connection的shutdown，不能再进行发送
    if(state_ == kDisconnected){
        LOG_ERROR("disconnected, give up writing");
        return;
    }

    // 表示channel_第一次开始写数据，缓冲区没有可发送数据
    if(!channel_->isWriting() && outputBuffer_.readableBytes() == 0){
        nwrote = ::write(channel_->fd(), data, len);
        if(nwrote >= 0){
            remaining = len - nwrote;
            if(remaining == 0 && writeCompleteCallback_){
                // 既然在这里把数据全部发送完成了，就不用给channel设置epollout属性了
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }else{
            nwrote = 0;
            if(errno != EWOULDBLOCK){
                LOG_ERROR("TcpConnection::sendInLoop");
                if(errno == EPIPE || errno == ECONNRESET){ // SIGPIPE  RESET
                    faultError = true;
                }
            }
        }
    }

    // 说明当前这一次write, 并没有把数据全部发送出去，剩余的数据需要保存到缓冲区中
    // 然后给channel注册epollout事件, poller发现tcp的发送缓冲区有空间, 会通知相应的sock(channel), 调用handleWrite回调方法
    // 也就是调用TcpConnection::handleWrite方法, 把发送缓冲区中的数据全部发送
    if(!faultError && remaining > 0){
        // 目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if(oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_){
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        outputBuffer_.append((char *)data + nwrote, remaining);
        if(!channel_->isWriting()){
            channel_->enableWriting(); // 这里一定要注册channel的写事件, 否则poller不会给channel通知epollout
        }
    }
}

void TcpConnection::shutdown(){
    if(state_ == kConnected){
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop(){
    if(!channel_->isWriting()){ // 说明当前outputBuffer中的数据已经全部发送完成
        socket_->shutdownWrite(); // 关闭写端
    }
}

void TcpConnection::connectEstablished(){
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading(); //向Poller注册channel的epollin事件

    // 新连接建立，执行回调
    connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed(){
    if(state_ == kConnected){
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件，从poller中del掉
        connectionCallback_(shared_from_this());
    }

    channel_->remove();// 把channel从Poller中删除掉
}

void TcpConnection::handleRead(Timestamp receiveTime){
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if(n > 0){
        // 已建立连接的用户，有可读事件发生, 调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }else if(n == 0){
        handleClose();
    }else{
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite(){
    if(channel_->isWriting()){
        int savedError = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedError);
        if(n > 0){
            outputBuffer_.retrieve(n);
            if(outputBuffer_.readableBytes() == 0){
                channel_->disableWriting();
                if(writeCompleteCallback_){
                    // 个人认为这里肯定不是为了让!isInLoopThread()成立，而是这个writeCompleteCallback_不是当前要紧事，所以放在pendingFunctors_，反正每次loop都会执行
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }

                if(state_ == kDisconnecting){
                    shutdownInLoop();
                }
            }
        }else{
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }else{
        LOG_ERROR("TcpConnection fd = %d is down, no more writing \n", channel_->fd()); 
    }
}

void TcpConnection::handleClose(){
    LOG_INFO("TcpConnection::handleClose fd = %d state = %d \n", channel_->fd(), state_.load());
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);
    closeCallback_(connPtr);
}

void TcpConnection::handleError(){
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if(getsockopt(channel_->fd(), SOL_SOCKET,SO_ERROR,&optval,&optlen) < 0){
        err = errno;
    }else{
        err = optval;
    }

    LOG_ERROR("TcpConnection::handleError name: %s - SO_ERROR: %d\n", name_.data(), err);
}
