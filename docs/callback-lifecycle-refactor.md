# 回调精简与 TcpConnection 生命周期重构

> 重构日期：2026-10-31
> 基线：`e0b6cfe` 之后的回调与生命周期重构工作区
> 目标：删除内部固定关系上的刻意回调封装，统一 TcpConnection 生命周期，并升级到 C++17

## 1. 重构目标

本轮重构遵循以下边界：

1. 应用层需要的回调继续保留。
2. Channel 的底层事件回调继续保留。
3. Acceptor、TcpConnection 与 TcpServer 之间的固定关系改为直接方法调用。
4. 不引入新的 Handler、Executor 或 ConnectionOwner 类。
5. 跨线程操作通过所属 EventLoop 的任务队列执行。
6. TcpConnection 状态只在其 IO 线程修改。
7. 用 lambda 和所有权明确的捕获替代内部 `std::bind`。
8. 将项目基线升级到 C++17。

## 2. 删除的回调

| 原回调 | 原调用链 | 当前实现 |
|---|---|---|
| `Acceptor::newConnectionCallback_` | Acceptor -> callback -> TcpServer | Acceptor 持有 `TcpServer&`，直接调用 `newConnection()` |
| `TcpConnection::closeCallback_` | TcpConnection -> callback -> TcpServer | TcpConnection 持有 `TcpServer&`，直接调用 `removeConnection()` |
| `TcpConnection::highWaterMarkCallback_` | TcpConnection -> callback | 删除未完整暴露的回调和阈值字段 |
| `TcpServer::threadInitCallback_` | TcpServer -> ThreadPool -> EventLoopThread | 删除整条三层回调传递 |

同步删除了：

- `NewConnectionCallback`
- `CloseCallback`
- `HighWaterMarkCallback`
- `ThreadInitCallback`
- Acceptor 的新连接回调 setter
- TcpConnection 的 close/highwater setter
- TcpServer、TcpConnection 中对应的 `std::bind`

## 3. 保留的回调

应用层回调：

```cpp
using ConnectionCallback =
    std::function<void(const TcpConnectionPtr&)>;
using MessageCallback =
    std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>;
using WriteCompleteCallback =
    std::function<void(const TcpConnectionPtr&)>;
```

对应 TcpServer 公开配置：

```cpp
setConnectionCallback
setMessageCallback
setWriteCompleteCallback
```

这些回调代表用户可配置策略，不在本轮删除。

Channel 底层事件回调：

```cpp
setReadCallback
setWriteCallback
setCloseCallback
setErrorCallback
```

Channel 同时被 EventLoop、Acceptor 和 TcpConnection 使用。若彻底删除这四个回调且不引入新接口，只能让 Channel 通过 `void* + 类型枚举`、`std::variant` 或模板识别具体 owner，反而增加耦合，因此保留。

EventLoop 的任务类型 `Functor = std::function<void()>` 也保留。任务队列需要接受不同生产者提交的工作，和组件固定关系不是同一类抽象。

## 4. 新的直接调用关系

```text
Acceptor::handleRead()
  -> TcpServer::newConnection()

TcpConnection::closeInLoop()
  -> TcpServer::removeConnection()

Channel
  -> read/write/close/error callback
  -> TcpConnection 的 weak_ptr lambda
```

Acceptor 是 TcpServer 的成员，使用引用表明生命周期从属关系：

```cpp
class Acceptor {
    TcpServer &server_;
};
```

TcpConnection 由 TcpServer 的连接表长期持有，也保存所属 TcpServer 引用：

```cpp
class TcpConnection {
    TcpServer &server_;
};
```

为了避免扩大公开接口，以下方法保持 private，并通过 `friend` 调用：

- `TcpServer::newConnection()`
- `TcpServer::removeConnection()`
- `TcpConnection::connectEstablished()`
- `TcpConnection::connectDestroyed()`

## 5. TcpConnection 生命周期

### 5.1 状态机

状态改为强类型枚举：

```cpp
enum class StateE {
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
};
```

状态转换只允许发生在连接所属 EventLoop 线程。

### 5.2 跨线程 send

公开接口改为值语义：

```cpp
void send(std::string data);
```

实现同时捕获连接和数据的所有权：

```cpp
auto self = shared_from_this();
loop_->runInLoop(
    [self, data = std::move(data)]() mutable {
        if (self->state_.load() == StateE::Connected) {
            self->sendInLoop(data);
        }
    });
```

这解决了原实现保存 `buf.data()` 裸地址造成的悬空访问：

- `self` 保证连接对象在任务执行期间存在。
- `data` 保证字节内容在任务执行期间存在。
- 任务执行前重新检查连接状态。

### 5.3 关闭命令

`shutdown()` 同样捕获 `self`，把状态检查和 socket 操作投递到连接所属 loop：

```text
Connected
  -> Disconnecting
  -> 输出缓冲写完
  -> shutdown(SHUT_WR)
  -> 对端 EOF/HUP
  -> Disconnected
```

主动关闭、对端关闭和错误关闭最终都进入：

```cpp
void TcpConnection::closeInLoop();
```

关闭路径统一完成：

1. 已关闭时直接返回。
2. 设置为 Disconnected。
3. 禁用 Channel 的全部事件。
4. 用户连接回调最多触发一次。
5. 调用 TcpServer 移除连接。
6. 在连接 IO loop 中移除 Channel。
7. 最后一个 shared_ptr 释放后关闭 Socket。

### 5.4 Channel 使用 weak self

Channel 回调保存 `weak_ptr<TcpConnection>`：

```cpp
auto weakSelf = weak_from_this();

channel_->setReadCallback(
    [weakSelf](Timestamp receiveTime) {
        if (auto self = weakSelf.lock()) {
            self->handleRead(receiveTime);
        }
    });
```

写、关闭和错误回调采用相同方式。

因此删除了原来的 `Channel::tie()`、`tie_` 和 `tied_` 间接保护机制：

- 不再保存 `weak_ptr<void>` 后再二次分发。
- 不再形成 `TcpConnection -> Channel -> std::function -> TcpConnection` 强引用环。
- Channel 销毁后，未执行 lambda 中的 weak_ptr 自然失效。

## 6. EventLoop 更新

1. `__thread` 改为标准 C++17 `thread_local`。
2. 增加 `assertInLoopThread()`。
3. Channel 修改、连接状态操作和 loop 运行增加线程断言。
4. 单个 Channel 回调和 pending task 异常由 EventLoop 捕获。
5. loop 退出后再执行一次 pending functors，使连接销毁任务有机会完成。
6. wakeup Channel 和 EventLoopThread 的内部 `std::bind` 改为 lambda。

需要保持的生命周期约束：

```text
EventLoop 必须晚于所属 TcpServer 和 TcpConnection 释放
TcpServer 析构时必须先向连接所属 loop 投递 connectDestroyed
IO 线程停止前必须执行连接清理任务
```

## 7. 现代 C++ 更新

项目编译基线从 C++11 升级到 C++17。

已替换：

| 原实现 | 新实现 |
|---|---|
| `char buf[128]` 时间格式化 | `std::array<char, 64>` |
| `char buf[1024]` 日志宏 | `std::array<char, 1024>` |
| `char buf[64]` 地址格式化 | `std::array<char, INET_ADDRSTRLEN>` |
| `char buf[32]` 线程名 | `std::array<char, 32>` |
| `char extrabuf[65536]` | `std::array<char, 64 * 1024>` |
| `iovec vec[2]` | `std::array<iovec, 2>` |
| C 风格 socket 地址转换 | `reinterpret_cast` |
| `memset` 初始化 POD | `{}` 值初始化 |
| socket 写操作 | 使用 `MSG_NOSIGNAL`，避免对端关闭触发 SIGPIPE |
| `__thread` | `thread_local` |
| `new` 创建对象 | `make_unique` / `make_shared` |

保留系统 ABI 相关布局，例如 `sockaddr_in`、`epoll_event`、`iovec`。

## 8. Timestamp 和 Logger

### Timestamp

`Timestamp::now()` 修正为真实微秒：

```cpp
std::chrono::system_clock::now().time_since_epoch()
```

格式化使用线程安全的 `localtime_r`，并输出微秒部分。

### Logger

- `LogLevel` 改为 `enum class`。
- 日志级别作为每次调用的参数，不再修改单例共享级别。
- Logger 内部增加 mutex，避免多 IO 线程输出交错。
- 格式化缓冲区改为 `std::array`。
- 宏内部变量使用不会与调用方名称冲突的独立名称。

## 9. 文件变化

主要修改：

```text
CMakeLists.txt
README.md
examples/echo_server.cpp

include/my_muduo/base/CurrentThread.h
include/my_muduo/base/Logger.h
include/my_muduo/net/Acceptor.h
include/my_muduo/net/Buffer.h
include/my_muduo/net/Callbacks.h
include/my_muduo/net/Channel.h
include/my_muduo/net/EventLoop.h
include/my_muduo/net/EventLoopThread.h
include/my_muduo/net/EventLoopThreadPool.h
include/my_muduo/net/InetAddress.h
include/my_muduo/net/TcpConnection.h
include/my_muduo/net/TcpServer.h

src/base/CurrentThread.cc
src/base/Logger.cc
src/base/Thread.cc
src/base/Timestamp.cc
src/net/Acceptor.cc
src/net/Buffer.cc
src/net/Channel.cc
src/net/EventLoop.cc
src/net/EventLoopThread.cc
src/net/EventLoopThreadPool.cc
src/net/EPollPoller.cc
src/net/InetAddress.cc
src/net/Socket.cc
src/net/TcpConnection.cc
src/net/TcpServer.cc
```

## 10. 验证结果

已完成的静态验证：

- 除保留的 Channel/应用层回调外，无内部 `std::bind`。
- 已删除 Acceptor、TcpConnection 内部 close callback 和 ThreadInit 回调链。
- 已删除 Channel tie 机制。
- 已确认项目编译特性为 C++17。
- Timestamp、Logger、Thread 已在可用编译器中通过 C++17 `-Wall -Wextra -Wpedantic` 语法检查。
- Timestamp/Logger 最小运行测试输出当前日期，确认 `Timestamp::now()` 已使用真实微秒。
- 四线程并发 Logger 烟雾测试通过，日志行未交错。
- 使用仅用于语法检查的最小 Linux/POSIX 声明，全部 `src/` 和示例已通过 C++17 全量 `-fsyntax-only` 检查，无警告。
- `git diff --check` 无格式错误。

当前限制：

- 宿主为 Windows，可用 MinGW 缺少 Linux `epoll`、`eventfd`、`accept4` 和 `sys/uio.h` 头文件。
- WSL 当前不可访问，因此本轮未执行完整 Linux 构建、Echo 运行测试或 ASan/TSan。
- 后续必须在 Linux 环境执行全量构建和并发关闭回归。

## 11. 后续验证重点

1. 跨线程发送临时字符串后立即关闭连接。
2. 对端停止读取，强制部分写和 `EPOLLOUT`。
3. 发送后立即 `shutdown()`，确认数据写完再关闭写端。
4. 对端 EOF、RST、`EPOLLERR` 与主动关闭同时发生。
5. TcpServer 析构时，所有连接在所属 IO 线程完成 Channel 移除。
6. 多线程同时输出日志，检查日志级别和内容不交错。
7. 使用 ASan 检查连接、数据和 lambda 生命周期。
8. 使用 TSan 检查 EventLoop 和 TcpServer 跨线程访问。

## 12. 设计约束

本次没有新增 Handler 抽象，因此以下直接依赖必须长期遵守：

```text
Acceptor 必须由 TcpServer 拥有
TcpConnection 必须由 TcpServer 连接表持有
TcpServer 必须晚于连接清理任务完成
Channel 必须由其父对象拥有，并先移出 Poller 再销毁
EventLoop 必须晚于其管理对象释放
```

只要这些所有权约束成立，直接引用和 lambda 捕获就是明确的，不需要额外的类型擦除接口。
