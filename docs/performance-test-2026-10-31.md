# my_muduo 性能测试报告

> 测试日期：2026-10-31
> 测试环境：本机 VMware Ubuntu 26.04 虚拟机
> 测试对象：重构后的 my_muduo 网络库

## 1. 测试结论

在 4 vCPU、约 3.3 GiB 可用内存的 Ubuntu 26.04 虚拟机上，my_muduo 完成了：

- Release 模式编译。
- 3 轮 64 字节持久连接 Echo 延迟测试，每轮 5000 次往返。
- 3 轮 32/64/128 并发吞吐测试。
- 3 轮串行连接建立、Echo、关闭测试，每轮 2000 个连接。
- 0、1、3 个 IO 线程的横向对比。

主要结果：

| 指标 | 结果 |
|---|---:|
| Echo 延迟 p50 | 约 50.8 μs |
| Echo 延迟 p95 | 约 135.7 μs |
| Echo 延迟 p99 | 约 224.8 μs |
| 32 并发 / 1 KiB 往返吞吐 | 约 141.8 MiB/s，约 72.6k 往返/s |
| 64 并发 / 4 KiB 往返吞吐 | 约 573.5 MiB/s，约 73.4k 往返/s |
| 128 并发 / 4 KiB 往返吞吐 | 约 554.6 MiB/s，约 71.0k 往返/s |
| 串行连接建立与关闭 | 约 4151 连接/s |

这些数字代表同机 loopback 的应用层 Echo 性能，不代表跨物理网络、TLS、复杂业务或持久化场景的能力。

## 2. 测试环境

| 项目 | 配置 |
|---|---|
| 虚拟化 | VMware Workstation |
| Guest OS | Ubuntu 26.04 LTS |
| Kernel | `7.0.0-38-generic x86_64` |
| CPU | 4 vCPU |
| 内存 | VM 配置 4 GiB，Guest 可见约 3.3 GiB |
| 测试时可用内存 | 约 1.8 GiB |
| 编译器 | GCC/G++ 15.2.0 |
| 构建类型 | Release |
| C++ 标准 | C++17 |
| 网络路径 | 127.0.0.1 loopback |
| Server IO 线程 | 3 个 worker loop + 主 loop |

构建产物：

```text
libmy_muduo.so
bench_server
bench_client
```

## 3. 测试方法

测试使用独立的 `bench_server` 和 `bench_client`，二者链接当前 my_muduo 源码构建出的动态库。

基准服务器：

```text
EventLoop
  -> TcpServer
  -> setThreadNum(3)
  -> onMessage: retrieveAllAsString + send
```

与 examples/echo_server.cpp 不同，基准服务器保持长连接，不在每次 Echo 后关闭写端，因此可以测试连续往返性能。

### 3.1 延迟测试

- 每个连接先进行 100 次预热往返。
- 然后执行 5000 次 64 字节请求/响应往返。
- 每轮记录平均值、p50、p95、p99。
- 测试执行 3 轮。

### 3.2 吞吐测试

使用多个客户端线程，每个线程建立一条连接：

1. 发送固定长度数据。
2. 等待服务器回传相同长度数据。
3. 连续执行指定次数。

测试负载：

```text
32  并发连接 x 512 次 x 1024 字节
64  并发连接 x 256 次 x 4096 字节
128 并发连接 x 128 次 x 4096 字节
```

吞吐按请求和响应两个方向合计展示。单方向应用字节数为表中 `MiB` 的一半。

### 3.3 连接抖动测试

- 单客户端串行执行 2000 次：

```text
connect
  -> send 32 bytes
  -> read 32 bytes
  -> close
```

- 统计完整连接建立、Echo、关闭链条的连接速率。

### 3.4 线程扩展测试

使用相同 64 并发、256 次、4 KiB 负载，分别测试：

```text
setThreadNum(0)：只使用主 EventLoop
setThreadNum(1)：主 EventLoop + 1 个工作 EventLoop
setThreadNum(3)：主 EventLoop + 3 个工作 EventLoop
```

每种配置执行 3 轮。

## 4. 延迟结果

3 个工作 IO 线程、64 字节、5000 次往返：

| 轮次 | 平均 | p50 | p95 | p99 |
|---|---:|---:|---:|---:|
| 1 | 61.18 μs | 50.36 μs | 113.32 μs | 219.89 μs |
| 2 | 71.91 μs | 50.86 μs | 175.45 μs | 267.05 μs |
| 3 | 65.47 μs | 50.78 μs | 135.72 μs | 224.83 μs |
| 中位数 | 65.47 μs | 50.78 μs | 135.72 μs | 224.83 μs |

判断：

- p50 稳定在约 50 μs，说明常见 loopback 往返开销较稳定。
- p95 和 p99 波动明显，可能受 JVM/桌面 VM 调度、日志输出、system call 和客户端线程抢占影响。
- 这些数据包含每一轮 EventLoop/Channel 的 INFO 日志。

## 5. 吞吐结果

### 5.1 3 个工作 IO 线程

| 并发 | 负载 | 单方向数据 | 往返 MiB/s | 往返/s |
|---:|---|---:|---:|---:|
| 32 | 512 x 1 KiB | 16 MiB | 141.90 | 72,652 |
| 32 | 512 x 1 KiB | 16 MiB | 139.71 | 71,530 |
| 32 | 512 x 1 KiB | 16 MiB | 141.82 | 72,609 |
| 64 | 256 x 4 KiB | 64 MiB | 578.82 | 74,089 |
| 64 | 256 x 4 KiB | 64 MiB | 573.51 | 73,409 |
| 64 | 256 x 4 KiB | 64 MiB | 559.72 | 71,644 |
| 128 | 128 x 4 KiB | 64 MiB | 578.95 | 74,106 |
| 128 | 128 x 4 KiB | 64 MiB | 531.76 | 68,065 |
| 128 | 128 x 4 KiB | 64 MiB | 554.56 | 70,984 |

中位数：

| 并发 | 往返 MiB/s | 往返/s |
|---:|---:|---:|
| 32 x 1 KiB | 141.82 | 72,609 |
| 64 x 4 KiB | 573.51 | 73,409 |
| 128 x 4 KiB | 554.56 | 70,984 |

判断：

- 1 KiB 与 4 KiB 配置的往返次数接近，说明当前主要受每轮请求/响应的系统调用和事件分发成本影响。
- 增大 payload 后总字节吞吐明显提高。
- 128 并发没有超过 64 并发，说明在 4 vCPU 和当前日志配置下继续增加并发没有带来吞吐收益。
- 测试客户端与服务器运行在同一 VM，会竞争同样的 4 个 vCPU。

## 6. 连接建立速率

3 个工作 IO 线程、串行连接测试：

| 轮次 | 连接数 | 时间 | 连接/s |
|---|---:|---:|---:|
| 1 | 2000 | 0.4818 s | 4150.93 |
| 2 | 2000 | 0.5260 s | 3802.34 |
| 3 | 2000 | 0.4797 s | 4169.46 |
| 中位数 | 2000 | 约 0.480 s | 4150.93 |

总计 6000 次连接、Echo 和关闭全部完成。

判断：

- 当前连接建立和注销链可以稳定完成。
- 跨主 loop/工作 loop 的连接表移除和 Channel 清理引入了额外线程唤醒。
- 这是单客户端串行结果，不代表 accept 队列或大并发建连上限。

## 7. IO 线程扩展对比

64 并发、256 次、4 KiB：

| IO 线程 | 往返 MiB/s 中位数 | 往返/s 中位数 | 连接/s 中位数 |
|---:|---:|---:|---:|
| 0 | 376.46 | 48,186.7 | 6643.17 |
| 1 | 353.93 | 45,302.4 | 4048.74 |
| 3 | 573.51 | 73,409.2 | 4150.93 |

判断：

- 3 个 worker loop 的吞吐明显高于 0 和 1。
- `setThreadNum(0)` 的单 loop 连接建立速率最高，因为它不需要把连接移除任务从 worker loop 投递回主 loop。
- `setThreadNum(1)` 同时承担跨 loop 投递和单 worker 分发，连接建立速率低于单 loop。
- 结果说明线程池主要改善了高并发连接吞吐，但小连接短任务会受到跨线程调度成本影响。

## 8. 测试中发现并修复的问题

第一次测试在就绪探测连接关闭后触发：

```text
EventLoop::assertInLoopThread failed
```

原因链路：

```text
TcpConnection 析构
  -> 无条件调用 Channel::remove()
  -> EventLoop::removeChannel()
  -> assertInLoopThread()
```

正常关闭路径已经由 `connectDestroyed()` 在所属 IO 线程完成 Channel 移除。析构函数再次调用 `remove()` 既重复，又可能在最后一个 shared_ptr 于其他线程释放时触发线程断言。

修复：

- `TcpConnection` 析构时不再访问 EventLoop 或 Poller。
- Channel 移除只由 `connectDestroyed()` 负责。
- 生命周期测试重新执行后，6000 次串行连接均成功。

## 9. 性能限制

本轮结果包含以下限制：

1. 服务器和客户端位于同一个 VM，使用 loopback，不经过物理网卡和外部交换机。
2. 客户端和服务器共享同一个 4 vCPU 虚拟机，客户端线程会抢占服务端 CPU。
3. 当前 Logger 在每次 Channel 事件和 Poller 返回时输出 INFO 日志，日志格式化和 mutex 会进入性能路径。
4. 没有使用 CPU 绑核、实时调度或关闭日志，因此数据不能视作网络库的极限性能。
5. 每轮吞吐测试较短，结果受 VM 调度和系统缓存影响较大。
6. 没有测试 TLS、应用层协议解析、持久化业务或大文件传输。
7. 没有进行内存占用增长、长时间稳定性或故障注入测试。

## 10. 后续测试建议

1. 增加可运行时关闭 INFO/DEBUG 日志的级别控制，分别测量“默认日志”和“关闭日志”性能。
2. 将客户端部署到另一台物理机或 VM，测试真实网络 RTT。
3. 使用 `perf stat`、`perf record` 检查 syscall、上下文切换和锁竞争。
4. 增加 10k/50k 长连接和持续发送测试。
5. 增加小包 burst、慢消费者、发送缓冲回流和断线重连测试。
6. 增加 1/4/8 IO 线程和不同 payload 的矩阵测试。
7. 分别使用 ASan、TSan 和 UBSan 跑功能压力用例。
8. 增加连接建立与销毁的并发测试，而不只是单客户端串行测试。

## 11. 复现方式

在 Linux 中构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMYMUDUO_BUILD_EXAMPLES=OFF
cmake --build build --parallel
```

测试服务器使用：

```text
TcpServer(threadCount)
onMessage:
  auto message = buffer->retrieveAllAsString();
  connection->send(std::move(message));
```

然后按上述并发、payload 和轮次数运行 `bench_client`。

## 12. 总体评价

在当前实现上，my_muduo 已经具备可运行、可扩展的多线程 TCP Echo 性能：

- loopback Echo p50 约 50 μs。
- 4 KiB、64 并发可达到约 570 MiB/s 的双向应用吞吐。
- 3 个工作 IO 线程明显改善持续吞吐。
- 6000 次连接建立、Echo、关闭生命周期测试全部成功。

当前最主要的性能瓶颈不是 epoll，而是每事件的日志输出、跨线程任务调度和同机客户端竞争。下一轮优化应先让日志级别可配置，再分离客户端与服务器环境进行基准测试。
