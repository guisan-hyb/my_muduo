#include <my_muduo/net/TcpConnection.h>

#include <my_muduo/base/Logger.h>
#include <my_muduo/net/Channel.h>
#include <my_muduo/net/EventLoop.h>
#include <my_muduo/net/Socket.h>
#include <my_muduo/net/TcpServer.h>

#include <cerrno>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace {

EventLoop *checkLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr) {
        LOG_FATAL("%s:%s:%d subloop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

bool isWouldBlock(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK;
}

} // namespace

TcpConnection::TcpConnection(EventLoop *loop, TcpServer &server,
                             const std::string &name, int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(checkLoopNotNull(loop))
    , server_(server)
    , name_(name)
    , state_(StateE::Connecting)
    , socket_(std::make_unique<Socket>(sockfd))
    , channel_(std::make_unique<Channel>(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
{
    LOG_INFO("TcpConnection::ctor[%s] at fd = %d\n", name_.data(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd = %d state = %d\n",
             name_.data(), channel_->fd(), static_cast<int>(state_.load()));

    channel_->remove();
}

void TcpConnection::send(std::string data)
{
    if (state_.load() != StateE::Connected) {
        return;
    }

    auto self = shared_from_this();
    loop_->runInLoop(
        [self, data = std::move(data)]() mutable {
            if (self->state_.load() == StateE::Connected) {
                self->sendInLoop(data);
            }
        });
}

void TcpConnection::sendInLoop(std::string_view data)
{
    loop_->assertInLoopThread();

    if (state_.load() != StateE::Connected) {
        return;
    }

    ssize_t wrote = 0;
    size_t remaining = data.size();
    bool faultError = false;

    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        wrote = ::send(channel_->fd(), data.data(), data.size(), MSG_NOSIGNAL);
        if (wrote >= 0) {
            remaining = data.size() - static_cast<size_t>(wrote);
            if (remaining == 0 && writeCompleteCallback_) {
                auto self = shared_from_this();
                loop_->queueInLoop([self] {
                    if (self->writeCompleteCallback_) {
                        self->writeCompleteCallback_(self);
                    }
                });
            }
        } else {
            wrote = 0;
            const int savedError = errno;
            if (!isWouldBlock(savedError)) {
                LOG_ERROR("TcpConnection::sendInLoop write error: %d\n", savedError);
                faultError = true;
                closeInLoop();
            }
        }
    }

    if (!faultError && remaining > 0 && state_.load() == StateE::Connected) {
        outputBuffer_.append(data.data() + wrote, remaining);
        if (!channel_->isWriting()) {
            channel_->enableWriting();
        }
    }
}

void TcpConnection::shutdown()
{
    auto self = shared_from_this();
    loop_->runInLoop([self] {
        if (self->state_.load() == StateE::Connected) {
            self->setState(StateE::Disconnecting);
            self->shutdownInLoop();
        }
    });
}

void TcpConnection::shutdownInLoop()
{
    loop_->assertInLoopThread();

    if (state_.load() == StateE::Disconnecting &&
        outputBuffer_.readableBytes() == 0) {
        socket_->shutdownWrite();
    }
}

void TcpConnection::connectEstablished()
{
    loop_->assertInLoopThread();

    if (state_.load() != StateE::Connecting) {
        return;
    }

    auto weakSelf = weak_from_this();
    channel_->setReadCallback([weakSelf](Timestamp receiveTime) {
        if (auto self = weakSelf.lock()) {
            self->handleRead(receiveTime);
        }
    });
    channel_->setWriteCallback([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->handleWrite();
        }
    });
    channel_->setCloseCallback([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->handleClose();
        }
    });
    channel_->setErrorCallback([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->handleError();
        }
    });

    setState(StateE::Connected);
    channel_->enableReading();

    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed()
{
    loop_->assertInLoopThread();

    if (state_.load() != StateE::Disconnected) {
        setState(StateE::Disconnected);
        channel_->disableAll();
        if (connectionCallback_) {
            connectionCallback_(shared_from_this());
        }
    }

    channel_->remove();
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    loop_->assertInLoopThread();

    if (state_.load() == StateE::Disconnected) {
        return;
    }

    int savedError = 0;
    const ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedError);
    if (n > 0) {
        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
        } else {
            inputBuffer_.retrieveAll();
        }
    } else if (n == 0) {
        handleClose();
    } else if (!isWouldBlock(savedError)) {
        errno = savedError;
        LOG_ERROR("TcpConnection::handleRead error: %d\n", savedError);
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    loop_->assertInLoopThread();

    if (!channel_->isWriting()) {
        return;
    }

    int savedError = 0;
    const ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedError);
    if (n > 0) {
        outputBuffer_.retrieve(static_cast<size_t>(n));
        if (outputBuffer_.readableBytes() == 0) {
            channel_->disableWriting();

            if (writeCompleteCallback_) {
                auto self = shared_from_this();
                loop_->queueInLoop([self] {
                    if (self->writeCompleteCallback_) {
                        self->writeCompleteCallback_(self);
                    }
                });
            }

            if (state_.load() == StateE::Disconnecting) {
                shutdownInLoop();
            }
        }
    } else if (!isWouldBlock(savedError)) {
        errno = savedError;
        LOG_ERROR("TcpConnection::handleWrite error: %d\n", savedError);
        handleError();
    }
}

void TcpConnection::handleClose()
{
    loop_->assertInLoopThread();
    LOG_INFO("TcpConnection::handleClose fd = %d state = %d\n",
             channel_->fd(), static_cast<int>(state_.load()));
    closeInLoop();
}

void TcpConnection::closeInLoop()
{
    loop_->assertInLoopThread();

    if (state_.load() == StateE::Disconnected) {
        return;
    }

    setState(StateE::Disconnected);
    channel_->disableAll();

    auto self = shared_from_this();
    if (connectionCallback_) {
        connectionCallback_(self);
    }
    server_.removeConnection(self);
}

void TcpConnection::handleError()
{
    loop_->assertInLoopThread();

    if (state_.load() == StateE::Disconnected) {
        return;
    }

    int optval = 0;
    socklen_t optlen = sizeof(optval);
    int error = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
        error = errno;
    } else {
        error = optval;
    }

    LOG_ERROR("TcpConnection::handleError name: %s - SO_ERROR: %d\n",
              name_.data(), error);
    closeInLoop();
}
