#include <my_muduo/net/TcpServer.h>
#include <my_muduo/base/Logger.h>

#include <string>
#include <utility>

class EchoServer{
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : loop_(loop), server_(loop, addr, name)
    {
        // 注册回调函数
        server_.setConnectionCallback([this](const TcpConnectionPtr &conn) {
            onConnection(conn);
        });
        server_.setMessageCallback(
            [this](const TcpConnectionPtr &conn, Buffer *buf, Timestamp time) {
                onMessage(conn, buf, time);
            });

        // 设置合适的loop线程数量  loopthread
        server_.setThreadNum(3);
    }

    void start(){
        server_.start();
    }

private:
    // 连接建立或者断开的回调
    void onConnection(const TcpConnectionPtr& conn){
        if(conn->connected()){
            LOG_INFO("connection UP : %s", conn->peerAddress().toIpPort().data());
        }else{
            LOG_INFO("connection DOWN : %s", conn->peerAddress().toIpPort().data());
        }
    }

    // 可读写事件的回调
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp){
        std::string msg = buf->retrieveAllAsString();
        conn->send(std::move(msg));
        conn->shutdown(); // 关闭写端 => EPOLLHUP => 统一关闭路径
    }

    EventLoop *loop_;
    TcpServer server_;
};



int main(){
    EventLoop loop;
    InetAddress addr(8000);
    EchoServer server(&loop, addr, "EchoServer_01"); // Acceptor non-blocking listenfd  create  bind

    server.start(); // listen  loopthread  listenfd => acceptChannel =>  mainLoop =>
    loop.loop(); // 启动mainLoop的底层Poller

    return 0;
}
