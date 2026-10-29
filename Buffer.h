#pragma once

#include <vector>
#include <string>
#include <algorithm>

// 网络库底层的缓冲区
// prependable bytes + readable bytes + writable bytes
// 0 <= readerIndex <= writerIndex <= size

class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend), readerIndex_(kCheapPrepend) ,writerIndex_(kCheapPrepend)
    {}

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }

    // 返回缓冲区中可读数据的起始地址
    const char* peek() const {
        return begin() + readerIndex_;
    }

    // onMessage string <= Buffer
    void retrieve(size_t len){
        if(len < readableBytes()){
            readerIndex_+=len; // 应用只读取了可读缓冲区的一部分(len), 还剩下readerIndex_+len -> writableIndex_
        }else{
            retrieveAll();
        }
    }

    void retrieveAll(){
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    // 把onMessage函数上报的Buffer数据，转成string类型的数据返回
    std::string retrieveAllAsString(){
        return retrieveAsString(readableBytes()); // 应用可读取数据的长度
    }

    std::string retrieveAsString(size_t len){
        std::string result(peek(), len);
        retrieve(len); // 上一句把缓冲区中可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
        return result;
    }

    // 考虑扩容
    void ensureWritableBytes(size_t len){
        if(writableBytes() < len){
            makeSpace(len); // 扩容函数
        }
    }

    // 把[data, data+len]内存上的数据，添加到writable缓冲区中
    void append(const char* data, size_t len){
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }

    char *beginWrite() { return begin() + writerIndex_; }
    const char *beginWrite() const { return begin() + writerIndex_; }

    // 从fd上读数据
    ssize_t readFd(int fd, int *saveError);
    // 从fd上发送数据
    ssize_t writeFd(int fd, int *saveError);

private:
    char* begin(){
        return buffer_.data(); // &*buffer_.begin()
    }
    const char *begin() const { return buffer_.data(); }

    void makeSpace(size_t len){
        // | kCheapPrepend | reader | writer |
        if(writableBytes()+prependableBytes() < len + kCheapPrepend){ // 确实得扩容
            buffer_.resize(writerIndex_ + len);
        }else{ // 还能蒸
            size_t readable = readableBytes();
            // 把剩余可读的拷到最开始的地方
            std::copy(begin() + readerIndex_,
                      begin() + writerIndex_,
                      begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};