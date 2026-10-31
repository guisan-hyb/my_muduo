# my_muduo

一个参考 muduo 实现的 Linux C++ TCP 网络库，采用 Reactor 和
one-loop-per-thread 模型。

## 项目结构

```text
include/my_muduo/base/   基础工具：时间、日志、线程、不可拷贝基类
include/my_muduo/net/    网络库公开接口，含Poller与EPollPoller
src/base/                基础工具实现
src/net/                 网络库实现
examples/                Echo 服务器示例
docs/                    架构与问题修复记录
```

公开头文件通过 `my_muduo` 根目录引用：

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

当前网络实现依赖 epoll、eventfd 和 accept4，不支持直接在 Windows 上编译。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

动态库位于 `build/libmy_muduo.so`，示例程序位于
`build/examples/echo_server`。

也可以使用脚本构建并安装到 `/usr/local`：

```bash
./autobuild.sh
```

通过环境变量可覆盖构建类型和安装位置：

```bash
BUILD_TYPE=Debug INSTALL_PREFIX="$HOME/.local" ./autobuild.sh
```

## 运行 Echo 示例

```bash
./build/examples/echo_server
```

服务器默认监听 `127.0.0.1:8000`。从另一个终端发送数据：

```bash
printf 'hello\n' | nc 127.0.0.1 8000
```

示例会把收到的数据发回，然后关闭连接的写端。

## 回调模型

- `TcpServer` 保留连接、消息和写完成三个应用层回调。
- `Channel` 保留读、写、关闭和错误四个底层事件回调。
- Acceptor 直接调用所属 TcpServer 的新连接方法。
- TcpConnection 直接调用所属 TcpServer 的连接移除方法。
- 跨线程发送和关闭通过所属 EventLoop 的任务队列执行。
- Channel 中 TcpConnection 的回调使用 `weak_ptr`，避免父子对象形成强引用环。

## 当前范围

当前已删除 Acceptor、TcpConnection 内部的固定回调封装，并将项目升级到
C++17。详细记录见 `docs/callback-lifecycle-refactor.md`。

定时器、客户端连接器、应用层拆包和自动化测试仍未实现。
