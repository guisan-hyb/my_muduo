# my_muduo

`my_muduo` 是一个参考 muduo 设计实现的 Linux C++ TCP 网络库。

项目采用：

```text
Reactor
+ one loop per thread
+ level-triggered epoll
+ 主 EventLoop accept
+ 工作 EventLoop 管理已建立连接
```

当前版本使用 C++17，完成了源码目录整理、内部回调精简和
TcpConnection 生命周期重构。

## 特性

- 基于 epoll 的非阻塞 TCP 服务器
- 主从 Reactor 和 EventLoop 线程池
- eventfd 跨线程唤醒
- 每线程任务队列
- Channel 事件抽象和 Poller 接口
- 输入/输出 Buffer 与部分写处理
- 连接状态机和统一关闭路径
- 跨线程 send 数据所有权保护
- 线程安全 Logger
- 微秒级 Timestamp
- CMake target、安装规则和 Echo 示例

## 架构概览

```text
应用层
  -> TcpServer 应用回调
       |
       v
TcpServer
  -> Acceptor: accept
  -> EventLoopThreadPool: 分配工作 loop
  -> TcpConnection: 管理连接
       |
       v
EventLoop
  -> epoll/Poller
  -> Channel
  -> pendingFunctors
```

一个 `TcpConnection` 永久绑定一个 `EventLoop`。连接运行期状态、
Channel 和 Buffer 只允许在所属 IO 线程修改。

完整架构见
[architecture-refactored.md](docs/architecture-refactored.md)。

## 项目结构

```text
include/my_muduo/base/   时间、日志、线程等基础组件
include/my_muduo/net/    网络库公开接口

src/base/                基础组件实现
src/net/                 网络库和 epoll 实现

examples/                Echo 示例
docs/                    架构、重构和性能文档
```

公开头文件使用稳定完整路径：

```cpp
#include <my_muduo/net/TcpServer.h>
#include <my_muduo/net/EventLoop.h>
#include <my_muduo/base/Logger.h>
```

## 环境要求

- Linux
- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- POSIX Threads

当前实现依赖 Linux 的 `epoll`、`eventfd`、`accept4` 和
`recv/send`，不能直接在原生 Windows 上编译。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

主要产物：

```text
build/libmy_muduo.so
build/examples/echo_server
```

只构建库：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMYMUDUO_BUILD_EXAMPLES=OFF
cmake --build build --parallel
```

## 安装

安装到自定义目录：

```bash
cmake --install build --prefix "$HOME/.local"
```

安装到 `/usr/local`：

```bash
./autobuild.sh
```

脚本默认使用 Release 和 `/usr/local`，执行安装时需要 `sudo`。
可通过环境变量覆盖：

```bash
BUILD_TYPE=Debug INSTALL_PREFIX="$HOME/.local" ./autobuild.sh
```

## 运行 Echo 示例

启动服务器：

```bash
./build/examples/echo_server
```

默认监听 `127.0.0.1:8000`，从另一个终端测试：

```bash
printf 'hello\n' | nc 127.0.0.1 8000
```

示例收到数据后会：

1. 消费输入 Buffer 的全部可读字节。
2. 原样发送回去。
3. 关闭连接写端。

它不是长连接协议解析示例。

## 最小使用示例

```cpp
#include <my_muduo/base/Timestamp.h>
#include <my_muduo/net/Buffer.h>
#include <my_muduo/net/EventLoop.h>
#include <my_muduo/net/InetAddress.h>
#include <my_muduo/net/TcpConnection.h>
#include <my_muduo/net/TcpServer.h>

#include <string>
#include <utility>

int main()
{
    EventLoop loop;
    InetAddress address(8000);
    TcpServer server(&loop, address, "demo-server");

    server.setThreadNum(3);
    server.setMessageCallback(
        [](const TcpConnectionPtr &connection, Buffer *buffer, Timestamp) {
            auto message = buffer->retrieveAllAsString();
            connection->send(std::move(message));
        });

    server.start();
    loop.loop();
    return 0;
}
```

`setMessageCallback` 中的 Buffer 只在回调期间有效。需要异步处理时，
必须复制或移动数据，不能保存 Buffer 指针。

## 回调模型

保留的应用层回调：

```cpp
setConnectionCallback
setMessageCallback
setWriteCompleteCallback
```

保留的 Channel 底层事件回调：

```cpp
setReadCallback
setWriteCallback
setCloseCallback
setErrorCallback
```

以下内部关系不再通过可替换回调传递：

```text
Acceptor -> TcpServer::newConnection()
TcpConnection -> TcpServer::removeConnection()
```

TcpConnection 写入 Channel 的四个事件 callback 使用
`weak_from_this()`，避免形成强引用环。

## 线程与生命周期

关键约束：

1. 一个线程最多运行一个 EventLoop。
2. TcpConnection 绑定一个 EventLoop，连接操作在该 loop 串行执行。
3. 跨线程 `send` 和 `shutdown` 通过 EventLoop 任务队列投递。
4. `send(std::string)` 由异步任务拥有数据，避免裸指针悬空。
5. 主动关闭、EOF、HUP 和错误通过统一关闭路径收敛。
6. Channel 必须先移出 Poller 再销毁。
7. TcpServer 和 EventLoop 必须晚于所属连接清理任务销毁。

生命周期重构细节见
[callback-lifecycle-refactor.md](docs/callback-lifecycle-refactor.md)。

## 性能参考

在本机 Ubuntu 26.04、4 vCPU、约 3.3 GiB 可用内存的 VM 中，
Release 构建、3 个 IO 工作线程、loopback 测得：

| 指标 | 中位数 |
|---|---:|
| 64 字节 Echo p50 | 约 50.8 μs |
| Echo p95 | 约 135.7 μs |
| Echo p99 | 约 224.8 μs |
| 32 并发 × 1 KiB | 约 141.8 MiB/s 往返 |
| 64 并发 × 4 KiB | 约 573.5 MiB/s 往返 |
| 串行连接建立/关闭 | 约 4151 连接/s |

测试包含默认 INFO 日志，并且客户端与服务器位于同一 VM，不代表
物理网络或关闭日志后的极限性能。

完整数据见
[performance-test-2026-10-31.md](docs/performance-test-2026-10-31.md)。

## 当前限制

- 没有 TimerQueue 和定时器 API。
- 没有 TcpClient、Connector 或非阻塞 connect。
- 没有应用层拆包、组包和协议 Codec。
- 没有发送队列容量限制或完整背压接口。
- 没有自动化 tests/、ASan 或 TSan 回归套件。
- 任务队列尚未提供显式停止、取消和容量策略。
- CMake 暂未导出 `find_package` 配置文件。

## 文档

- [重构后架构与流程](docs/architecture-refactored.md)
- [回调与生命周期重构](docs/callback-lifecycle-refactor.md)
- [Linux 性能测试](docs/performance-test-2026-10-31.md)
- [历史架构文档](docs/architecture.md)
- [Channel 事件位修复](docs/fixes/channel-event-mask.md)

## 设计目标

本项目的首要目标是理解并实现一个结构清晰、可调试的
Reactor 网络库。它适合学习和架构复盘，在补齐协议层、背压、
测试和长时间稳定性验证之前，不应直接用于关键生产业务。
