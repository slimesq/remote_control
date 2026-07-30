# Claude Code 项目说明

本文件只提供项目文档入口，避免重复维护构建配置、架构说明和编码规则。

## 开始工作前

- 构建、运行、VS Code、Qt Creator 和命令行配置以 [README](README.md) 为准。
- 脚本参数和 CMake presets 以 [脚本说明](scripts/README.md) 为准。
- 架构、线程模型、协议和推荐阅读顺序参见
  [项目代码学习指南](docs/StudyGuide.md)。
- 编码与审查规则以
  [coding-style-review skill](.claude/skills/coding-style-review/SKILL.md) 为准。

## 文档维护约束

- 用户使用的配置只维护在 `README.md`，脚本实现细节只维护在 `scripts/README.md`。
- 架构、线程或协议发生变化时，同步更新 `docs/StudyGuide.md`。
- 编码规则发生变化时，只更新项目 skill，不在其他 Markdown 文件中复制规则。
- 项目当前仅面向 Windows、MSVC、Ninja、Qt 5.15/Qt 6 和 C++17。
