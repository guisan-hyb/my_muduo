#include <my_muduo/net/Buffer.h>

#include <array>
#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

// 从fd上读取数据  Poller工作在LT模式
// Buffer缓冲区是有大小的！ 但是从fd上读取数据的时候，却不知道tcp数据最终的大小
ssize_t Buffer::readFd(int fd, int *saveError)
{
    std::array<char, 64 * 1024> extrabuf{};
    std::array<iovec, 2> vec{};
    const size_t writable = writableBytes(); // 这是Buffer底层缓冲区剩余的可写大小
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf.data();
    vec[1].iov_len = sizeof(extrabuf);

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = readv(fd, vec.data(), iovcnt);
    if(n < 0){
        *saveError = errno;
    }else if(n >= 0 && static_cast<size_t>(n) <= writable){ // Buffer的可写缓冲区已经够存储读出来的数据了
        writerIndex_ += n;
    }else{ //extrabuf中也写入了数据
        writerIndex_ = buffer_.size();
        append(extrabuf.data(), n - writable); // writerIndex_ 开始写 n - writable 大小的数据
    }

    return n;
}


ssize_t Buffer::writeFd(int fd, int *saveError)
{
    ssize_t n = ::send(fd, peek(), readableBytes(), MSG_NOSIGNAL);
    if(n < 0){
        *saveError = errno;
    }
    return n;
}

