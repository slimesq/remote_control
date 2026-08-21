# Claude Code 项目说明

本文件提供项目文档入口和文档维护边界，避免重复维护构建配置、架构说明和编码规则。

## 开始工作前

- 构建、运行、VS Code、Qt Creator 和命令行配置以 [README](README.md) 为准。
- 脚本参数和 CMake presets 以 [脚本说明](scripts/README.md) 为准。
- 项目功能及其主要实现技术参见[项目功能与技术实现](docs/FeaturesAndDesign.md)。
- 跨模块设计原则、模式归类、不变量和工程取舍参见
  [设计思想与设计模式](docs/DesignPrinciples.md)。
- 推荐阅读顺序参见 [项目代码学习指南](docs/StudyGuide.md)。
- Packet、命令和 payload 布局参见 [远程控制协议参考](docs/ProtocolReference.md)。
- 客户端组件、线程模型和网络通道参见
  [客户端系统架构](docs/ClientArchitecture.md)。
- 服务端 IOCP、任务池和连接生命周期参见
  [IOCP 服务端系统架构](docs/ServerArchitecture.md)。
- 编码与审查规则以
  [coding-style-review skill](.claude/skills/coding-style-review/SKILL.md) 为准。

## 文档维护约束

- 面向用户的构建、运行、IDE、运行参数概览和完整测试矩阵维护在 `README.md`；脚本参数、
  preset 生成机制和脚本副作用维护在 `scripts/README.md`。
- 功能发生变化时更新 `docs/FeaturesAndDesign.md`；职责边界、并发模型、生命周期、不变量或设计取舍
  发生变化时更新 `docs/DesignPrinciples.md` 和对应架构文档；协议变化时更新协议参考。完成后检查
  `docs/StudyGuide.md` 中的阅读入口是否仍然准确。
- 通用 IOCP 概念、API 和基础练习由外部
  [IOCP 学习仓库](https://github.com/slimesq/IOCP) 维护；本仓库只维护与 `remote_control` 直接相关的
  IOCP 实现、并发不变量、源码映射和测试说明。
- 编码规则发生变化时，只更新项目 skill，不在其他 Markdown 文件中复制规则。
- `CMakeUserPresets.json`、项目根目录的 `compile_commands.json` 和 `build/` 都是本机生成物；
  它们不是共享配置的事实来源，也不应把其机器相关内容固化到文档中。
- 项目当前仅面向 Windows、MSVC、Ninja、Qt 5.15/Qt 6 和 C++17。
