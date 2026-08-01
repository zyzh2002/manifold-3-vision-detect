# 项目文档

本文档库面向**人类**维护者与接手者，使用中文编写。AI 代理的工作指南与内部产物位于 `AGENTS.md` 与 `.agents/docs/`（英文），不在此列。

## 文档清单

| 文档 | 内容 | 何时阅读 |
|---|---|---|
| [getting-started.md](getting-started.md) | 环境准备、构建、部署、运行、联调、排障 | 新人首次上手 / 搭建环境 |
| [project-status.md](project-status.md) | 各阶段完成状态与目标机验证记录摘要 | 了解项目进展 |
| [roadmap.md](roadmap.md) | 里程碑路线图与下一步计划 | 规划工作 / 了解方向 |
| [architecture.md](architecture.md) | 系统边界、数据流、模块边界与关键接口 | 理解代码结构 / 改代码前 |
| [build-environment.md](build-environment.md) | 工具链、sysroot、ABI、链接与验证规则 | 处理构建环境 / 交叉编译问题 |
| [development.md](development.md) | 分支策略、提交规范、测试、代码风格、联调流程 | 开始贡献代码前 |

## 阅读路径

**新人 / 接手者：** `getting-started.md` → `architecture.md` → `project-status.md`

**日常开发：** `development.md` + 对应模块的 `architecture.md` 章节

**详细技术决策与完整验证数据：** 见 `.agents/docs/`（AI 工作产物，英文，含完整里程碑计划、设计 spec 与实施计划；人类按需查阅）。

## 仓库根文档

- [README.md](../README.md) — 仓库快速入口（中文）
- [AGENTS.md](../AGENTS.md) — AI 代理工作指南（英文，人类无需阅读）
