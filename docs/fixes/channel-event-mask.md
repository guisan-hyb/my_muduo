# Channel 事件位判断修复

## 1. 问题概述

`Channel::isWriting()` 和 `Channel::isReading()` 原先使用完整相等判断：

```cpp
bool isWriting() const { return events_ == kWriteEvent; }
bool isReading() const { return events_ == kReadEvent; }
```

`events_` 保存的是一组 epoll 事件位，可以同时包含多个事件。使用 `==` 只能判断“事件集合是否恰好只有目标事件”，不能判断“事件集合是否包含目标事件”。

## 2. 触发原因

当前事件定义如下：

```cpp
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;
```

连接建立后，`TcpConnection::connectEstablished()` 会调用：

```cpp
channel_->enableReading();
```

假设对应的位值为：

```text
EPOLLIN  = 0x1
EPOLLPRI = 0x2
EPOLLOUT = 0x4
```

此时：

```text
events_ = EPOLLIN | EPOLLPRI = 0x3
```

发送数据只完成一部分时，`sendInLoop()` 会把剩余数据写入 `outputBuffer_`，然后调用：

```cpp
channel_->enableWriting();
```

`enableWriting()` 使用按位或，因此不会覆盖读事件：

```text
events_ = EPOLLIN | EPOLLPRI | EPOLLOUT = 0x7
```

此时 `events_` 已经包含写事件，但原实现比较的是：

```text
events_ == kWriteEvent
0x7 == 0x4
false
```

所以 `isWriting()` 错误返回 `false`。同理，读写事件同时存在时，`isReading()` 也会错误返回 `false`。

## 3. 对连接读写的影响

错误发生时，典型调用链为：

```text
sendInLoop()
  -> outputBuffer_ 保存未发送数据
  -> enableWriting()
  -> epoll 返回 EPOLLOUT
  -> Channel 调用 TcpConnection::handleWrite()
  -> handleWrite() 检查 channel_->isWriting()
  -> 错误得到 false
  -> 不刷新 outputBuffer_
```

可能造成以下后果：

1. 未发送数据一直留在 `outputBuffer_` 中。
2. `handleWrite()` 输出 “fd is down” 错误日志，但实际连接仍在工作。
3. 当前使用 level-triggered epoll，`EPOLLOUT` 未被取消时会持续上报，可能造成事件循环空转。
4. `shutdownInLoop()` 可能错误地认为发送缓冲已经清空，从而提前关闭写端并丢失数据。
5. 写完成回调无法按预期触发。

## 4. 修复方式

`events_` 是位集合，应使用按位与检查目标位是否非零：

```cpp
bool isWriting() const { return (events_ & kWriteEvent) != 0; }
bool isReading() const { return (events_ & kReadEvent) != 0; }
```

修复后的判断不要求 `events_` 只包含读写中的一种事件，而是能正确识别目标事件是否包含在当前事件集合中。

对比：

```cpp
// 错误：只有在“恰好只监听写事件”时才为真
events_ == kWriteEvent

// 正确：只要事件集合中包含写事件就为真
(events_ & kWriteEvent) != 0
```

`isNoneEvent()` 仍然使用 `events_ == kNoneEvent` 是合理的，因为它要判断的是所有事件位是否都为空。

## 5. 修改文件

- `Channel.h`
  - 修正 `isWriting()`。
  - 同步修正具有相同设计问题的 `isReading()`。

## 6. 验证记录

静态检查覆盖以下场景：

| 场景 | `events_` 示例 | 修复前 `isWriting()` | 修复后 `isWriting()` |
|---|---:|---:|---:|
| 仅读事件 | `0x3` | false | false |
| 读事件与写事件并存 | `0x7` | false | true |
| 仅写事件 | `0x4` | true | true |
| 无事件 | `0x0` | false | false |

结论：

- 读写事件并存是 `TcpConnection` 等待发送缓冲清空时的正常状态。
- 修复后 `handleWrite()` 能继续刷新输出缓冲。
- `disableWriting()` 清除写位后，`isWriting()` 会重新返回 false。
- `isReading()` 与 `isWriting()` 的语义保持一致。

## 7. 验证范围说明

本次修改属于局部事件位判断修正，已完成源码静态核对。当前 Windows 环境没有可用的 Linux/WSL 编译链，因此未在本机执行 `epoll` 运行时测试。后续可在 Linux 环境使用部分写、主动关闭和 Echo 示例进行完整回归。
