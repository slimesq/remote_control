# 远程控制协议参考

本文是客户端与服务端共享 TCP 协议的权威说明。协议枚举和 payload 编解码实现在
[`Protocol.h`](../include/common/Protocol.h) 与 [`Protocol.cpp`](../src/common/Protocol.cpp)，
Packet framing 实现在 [`Packet.h`](../include/common/Packet.h) 与
[`Packet.cpp`](../src/common/Packet.cpp)。学习代码的顺序参见
[项目代码学习指南](StudyGuide.md)，连接如何映射到线程参见
[客户端系统架构](ClientArchitecture.md)和[IOCP 服务端系统架构](ServerArchitecture.md)。

## 传输约定

- 使用 TCP，所有多字节整数均为 little-endian。
- TCP 是字节流；一个读取事件可能只包含半个 Packet，也可能包含多个连续 Packet。
- `Packet::tryParse()` 负责保留半包、拆分连续包，并从非法前缀、长度或校验值恢复同步。
- Packet 通用 payload 上限为 64 MiB。服务端命令请求都很小，因此服务端另外把单连接未处理
  输入限制为 1 MiB；截图和下载的大 payload 方向是服务端到客户端。
- 普通路径和状态消息通过 `QString::fromUtf8()` 容错解码；非法字节序列可被替换为
  Unicode replacement character，不会单独导致协议解析失败。`FileEntry` 文件名则使用
  下文定义的严格 UTF-8 校验。
- 当前协议没有认证、加密、重放保护或完整的协议版本协商，只适合受控学习环境。

## Packet 布局

| 偏移 | 大小 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 2 bytes | `Header` | 固定为 `0xFEFF`，线上字节为 `FF FE` |
| 2 | 4 bytes | `Length` | `Command + Payload + Checksum` 的长度，即 payload 大小加 4 |
| 6 | 2 bytes | `Command` | `Command` 枚举值 |
| 8 | N bytes | `Payload` | 命令相关数据，最大 64 MiB |
| 8 + N | 2 bytes | `Checksum` | payload 字节累加后保留低 16 位 |

校验值只用于发现传输或解析错误，不提供密码学完整性。解析器遇到非法 Packet 时会继续寻找
下一个 `FF FE`，因此损坏连接不一定需要立即断开。

## 命令和服务端连接分类

命令按服务端内部连接阶段分组如下。括号中的数字是命令在线上的枚举值。

- **`OneShot`**
  - `ListDrives`（`1`）：请求 payload 为空；响应是 UTF-8 编码、逗号分隔的盘符列表。
  - `RunFile`（`3`）：请求 payload 是 UTF-8 路径；响应使用通用状态 payload。
  - `TestConnection`（`1981`）：请求 payload 为空；返回同命令、空 payload。
- **`FileTransfer`**
  - `ListDirectory`（`2`）：请求 payload 是 UTF-8 路径；响应包含多个 `FileEntry` Packet。
  - `DownloadFile`（`4`）：请求 payload 是 UTF-8 路径；先返回文件长度 Packet，再返回数据
    Packet。
  - `DeleteFile`（`9`）：请求 payload 是 UTF-8 路径；响应使用通用状态 payload。
- **`ControlStream`**
  - `MouseEvent`（`5`）：请求 payload 是 `MouseEventPacket`；响应使用通用状态 payload。
  - `LockMachine`（`7`）：请求 payload 为空；响应使用通用状态 payload。
  - `UnlockMachine`（`8`）：请求 payload 为空；响应使用通用状态 payload。
- **`ScreenStream`**
  - `WatchScreen`（`6`）：请求 payload 为空；响应 payload 是 PNG 图像。
- **首包握手**
  - `ControlChannel`（`10`）：请求 payload 为空；响应使用通用状态 payload，随后连接进入
    `ControlStream`。

上述请求 payload 是客户端应当遵守的规范格式。当前服务端会显式验证需要空 payload 的
`WatchScreen`、`ControlChannel`、`LockMachine` 和 `UnlockMachine`；`ListDrives` 和
`TestConnection` 则会忽略额外 payload。后者是当前服务端的容错行为，不改变客户端
应发送空 payload 的协议要求。

这些连接阶段由服务端根据首个完整 Packet 推导，不是线上传输字段。连接分类后
不能切换到另一类业务。`ScreenStream` 收到
`WatchScreen` 以外的命令时按协议错误关闭连接；`ControlStream` 对鼠标、模拟锁定和解锁命令正常
响应，对其他命令返回失败状态并保留连接。`OneShot` 在最终响应发送完成后关闭；`FileTransfer`
在最终目录条目、文件数据或状态响应发送完成后关闭。

### 失败表示

- `ControlChannel`、`MouseEvent`、`LockMachine`、`UnlockMachine`、`RunFile` 和
  `DeleteFile` 通过通用状态 payload 返回可表示的业务失败。
- `ListDirectory` 使用 `isInvalid == true` 且 `hasNext == false` 的终止条目表示目录
  不可用。
- `DownloadFile` 在首个响应 Packet 中使用负的文件长度表示文件不可读；当前
  服务端发送 `-1`。
- `WatchScreen` 没有独立状态 Packet；截图、调度或传输失败时，服务端关闭连接，
  客户端将断开视为帧请求失败。
- `TestConnection` 和 `ListDrives` 没有命令级失败 payload。未知首包命令、
  `ScreenStream` 中的命令冲突或无法继续的 transport 错误通过关闭连接表示。

## 通用状态 payload

第一个字节是 `StatusCode`，其余字节是可选的 UTF-8 消息：

| 值 | 枚举 | 含义 |
| --- | --- | --- |
| `0` | `StatusCode::Failure` | 命令失败 |
| `1` | `StatusCode::Success` | 达到该命令定义的成功条件 |
| 其他值 | 未定义 | 客户端按失败处理 |

`parseStatusPayload()` 只有在第一个字节明确为 `StatusCode::Success` 时才返回 `true`；空 payload、
`Failure` 和未知状态值都返回 `false`。

状态码只编码成功或失败，具体成功条件由命令定义：

- `ControlChannel`：服务端已接受控制长连接握手。
- `MouseEvent`：对应的 Windows 鼠标定位或输入注入调用成功。
- `LockMachine`、`UnlockMachine`：请求已投递到服务端 GUI 线程，不表示界面状态已经切换完成。
- `RunFile`：Windows shell 已接受打开请求，不表示目标程序已经执行结束。
- `DeleteFile`：服务端已完整删除目标文件或目录树。

## `FileEntry` payload

| 字段 | 大小 | 说明 |
| --- | --- | --- |
| Version | 1 byte | 当前固定为 `1` |
| Flags | 1 byte | bit 0：无效；bit 1：目录；bit 2：后续还有条目 |
| NameLength | 4 bytes | UTF-8 名称的字节数 |
| FileName | N bytes | 不包含父目录的 UTF-8 文件名 |

接收端必须同时验证：

1. `Version` 精确等于 `1`。
2. `Flags` 只能使用 bit 0～bit 2，任何未知位都使整个条目无效。
3. `NameLength` 精确等于 payload 中剩余的字节数，不允许截断或尾随数据。
4. `FileName` 解码后重新编码的 UTF-8 字节必须与原始字节完全一致；否则按无效
   条目处理。

目录响应由零个或多个普通条目和一个 `hasNext == false` 的终止条目组成。正常终止条目的
`fileName` 为空，它只是结束标记，不表示目录内容。无效目录使用 `isInvalid == true` 且
`hasNext == false` 的单一条目结束请求。

## 下载 payload

下载响应的第一个 `DownloadFile` Packet 只包含一个 little-endian `qint64`：非负值表示文件
总字节数；当前服务端使用 `-1` 表示文件无法打开，客户端把任意负值都视为失败。成功后续
Packet 的 payload 是原始文件字节。协议没有最终状态 Packet；客户端累计收到声明长度后即使用
`QSaveFile::commit()` 原子替换目标文件。

协议没有单独的“取消下载”命令。客户端取消时会中止该下载连接并放弃 `QSaveFile` 临时内容；
服务端从 socket 错误或断开 completion 得知连接失效，并停止该连接的后续发送。客户端本地
使用 endpoint generation 和 download generation 过滤已经排队的旧进度、完成或取消结果，
这不会改变线上 Packet 格式。

## 鼠标 payload

`MouseEventPacket` 固定为 12 bytes，并使用 1-byte packing：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `action` | `quint16` | `MouseAction` 枚举值 |
| `button` | `quint16` | `MouseButton` 枚举值 |
| `x` | `qint32` | 远程屏幕绝对 x 坐标 |
| `y` | `qint32` | 远程屏幕绝对 y 坐标 |

纯移动使用 `MouseAction::Move + MouseButton::None`；点击、按下、释放和双击需要具体按钮。

`MouseAction` 的线上数值：

| 值 | 枚举 |
| ---: | --- |
| `0` | `MouseAction::Click` |
| `1` | `MouseAction::DoubleClick` |
| `2` | `MouseAction::Press` |
| `3` | `MouseAction::Release` |
| `4` | `MouseAction::Move` |

`MouseButton` 的线上数值：

| 值 | 枚举 |
| ---: | --- |
| `0` | `MouseButton::Left` |
| `1` | `MouseButton::Right` |
| `2` | `MouseButton::Middle` |
| `8` | `MouseButton::None` |

## 修改协议时的检查清单

1. 同步修改 `include/common` 与 `src/common` 的枚举和编解码。
2. 同步更新客户端请求、响应解析和服务端阶段路由。
3. 补充 `RemoteControlProtocolTests` 的边界测试。
4. 运行完整 CTest，再在受控环境运行 `RemoteControlSmokeTests`。
5. 更新本文的命令表和 payload 布局。
