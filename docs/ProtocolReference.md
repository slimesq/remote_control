# 远程控制协议参考

本文记录客户端与服务端共享的 TCP 协议。学习代码的顺序参见
[项目代码学习指南](StudyGuide.md)，连接如何映射到客户端和服务端线程参见对应的架构文档。

## 传输约定

- 使用 TCP，所有多字节整数均为 little-endian。
- TCP 是字节流；一个读取事件可能只包含半个 Packet，也可能包含多个连续 Packet。
- `Packet::tryParse()` 负责保留半包、拆分连续包，并从非法前缀、长度或校验值恢复同步。
- Packet 通用 payload 上限为 64 MiB。服务端命令请求都很小，因此服务端另外把单连接未处理
  输入限制为 1 MiB；截图和下载的大 payload 方向是服务端到客户端。
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

## 命令和连接类型

| 命令 | 值 | 连接阶段 | 请求 payload | 响应 |
| --- | ---: | --- | --- | --- |
| `ListDrives` | 1 | `OneShot` | 空 | UTF-8 编码、逗号分隔的盘符列表 |
| `ListDirectory` | 2 | `FileTransfer` | UTF-8 路径 | 多个 `FileEntry` Packet |
| `RunFile` | 3 | `OneShot` | UTF-8 路径 | 通用状态 payload |
| `DownloadFile` | 4 | `FileTransfer` | UTF-8 路径 | 文件长度 Packet，随后是数据 Packet |
| `MouseEvent` | 5 | `ControlStream` | `MouseEventPacket` | 通用状态 payload |
| `WatchScreen` | 6 | `ScreenStream` | 空 | PNG 图像 payload |
| `LockMachine` | 7 | `ControlStream` | 空 | 通用状态 payload |
| `UnlockMachine` | 8 | `ControlStream` | 空 | 通用状态 payload |
| `DeleteFile` | 9 | `FileTransfer` | UTF-8 路径 | 通用状态 payload |
| `ControlChannel` | 10 | 首包握手 | 空 | 通用状态 payload |
| `TestConnection` | 1981 | `OneShot` | 空 | 同命令、空 payload |

首个完整 Packet 决定服务端连接阶段。连接分类后不能切换到另一类业务：`ScreenStream` 只接受
`WatchScreen`，`ControlStream` 只接受鼠标、模拟锁定和解锁命令；一次性连接在最终响应发送后关闭。

## 通用状态 payload

第一个字节是 `StatusCode`，其余字节是可选的 UTF-8 消息：

| 值 | 枚举 | 含义 |
| --- | --- | --- |
| `0` | `StatusCode::Failure` | 命令失败 |
| `1` | `StatusCode::Success` | 命令成功 |
| 其他值 | 未定义 | 客户端按失败处理 |

`parseStatusPayload()` 只有在第一个字节明确为 `StatusCode::Success` 时才返回 `true`；空 payload、
`Failure` 和未知状态值都返回 `false`。

## `FileEntry` payload

| 字段 | 大小 | 说明 |
| --- | --- | --- |
| Version | 1 byte | 当前固定为 `1` |
| Flags | 1 byte | bit 0：无效；bit 1：目录；bit 2：后续还有条目 |
| NameLength | 4 bytes | UTF-8 名称的字节数 |
| FileName | N bytes | 不包含父目录的 UTF-8 文件名 |

目录响应由零个或多个普通条目和一个 `hasNext == false` 的终止条目组成。无效目录使用
`isInvalid == true` 且 `hasNext == false` 的单一条目结束请求。

## 下载 payload

下载响应的第一个 `DownloadFile` Packet 只包含一个 little-endian `qint64`：非负值表示文件
总字节数，负值表示文件无法打开。成功后续 Packet 的 payload 是原始文件字节；客户端累计收到
声明长度后使用 `QSaveFile::commit()` 原子替换目标文件。

## 鼠标 payload

`MouseEventPacket` 固定为 12 bytes，并使用 1-byte packing：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `action` | `quint16` | `MouseAction` 枚举值 |
| `button` | `quint16` | `MouseButton` 枚举值 |
| `x` | `qint32` | 远程屏幕绝对 x 坐标 |
| `y` | `qint32` | 远程屏幕绝对 y 坐标 |

纯移动使用 `MouseAction::Move + MouseButton::None`；点击、按下、释放和双击需要具体按钮。

## 修改协议时的检查清单

1. 同步修改 `include/common` 与 `src/common` 的枚举和编解码。
2. 同步更新客户端请求、响应解析和服务端阶段路由。
3. 补充 `RemoteControlProtocolTests` 的边界测试。
4. 运行完整 CTest，再在受控环境运行 `RemoteControlSmokeTests`。
5. 更新本文的命令表和 payload 布局。
