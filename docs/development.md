# 开发工作流

本文介绍给本仓库做贡献时的工作流约定：分支、提交、测试、代码风格、联调与代码评审。涉及 AI 代理的完整
指令（含 PSDK 研究流程、权限边界）见根目录 `AGENTS.md`（英文）。

## 分支策略

- 采用轻量级主干开发（trunk-based）：`main` 必须始终可构建、可验证。
- 常规实现工作：在 `main` 上开短生命周期分支，一个目标一个分支。
- 分支前缀：`feat/`、`fix/`、`docs/`、`chore/`、`refactor/`、`test/`，后跟 kebab-case 主题，
  例如 `feat/liveview-capture`、`docs/build-environment`。
- 不创建长期分支（`develop`、`integration`、`release` 等）。
- 集成前先把开发分支 rebase 到最新 `main`，不产生 merge commit。
- 合入 `main` 前需要明确授权；不要直接向 `main` 提交。

## 提交规范

- 提交信息使用英文，采用 Conventional Commits 前缀：`chore:`、`feat:`、`fix:`、`docs:`。
- 提交前确认子模块就绪：`git submodule update --init --recursive`。

## 测试

主机端单元测试覆盖 `src/core/`、`src/capture/`、`src/inference/` 与 `scripts/`：

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --test-dir build-host --output-on-failure
```

交叉编译的 ELF 静态校验随 `manifold3-cross-release` 构建自动执行（`tests/toolchain/`）。涉及行为的改动
建议遵循测试先行（TDD）：先写失败测试，再实现，最后验证通过。

## 代码风格

- **语言**：应用代码（`src/app/`、`src/capture/`、`src/inference/`）用 C++17；平台移植（`src/platform/`）用 C99。
- **格式**：LLVM 风格 clang-format，120 列上限。
- **命名**：文件 snake_case，类型 PascalCase，函数与变量 snake_case。
- **语言要求**：代码注释、提交信息与 agent 文档必须英文；`docs/` 下的人类文档使用中文。
  源代码中禁止中文注释与非英文标识符。
- PSDK C 头文件自带 `extern "C"` 保护，从 C++ 包含时无需额外包裹。

## PSDK 研究要求

任何涉及 DJI Payload SDK API、生命周期、回调语义、平台适配、Liveview 或示例用法的研究和实现，
在写代码前**必须先**调用 `psdk-deepwiki-research` 技能并遵循其研究流程（DeepWiki 文档 + 本地
PSDK 3.16.0 头文件/示例核对；冲突时以本地为准）。完整流程见 `AGENTS.md`。

## 联调流程

日常迭代使用直连部署（不依赖 DPK 安装）：

1. 交叉编译：`cmake --preset manifold3-cross-release && cmake --build --preset manifold3-cross-release`
2. 部署并前台运行：`./scripts/deploy.sh 192.168.42.120 run`（`--no-build` 可跳过重新编译）
3. 运行前停止机上 `Smart3DExplore`（`dji_app_ctl stop Smart3DExplore`），结束后恢复。

凭据通过 git 忽略的 `.local/credentials.env`（权限 600）注入；缺失时使用占位符（应用会报错退出）。

## 代码评审预期

- 每个可评审目标对应一个短生命周期分支；完成后提出评审。
- 评审关注：行为正确性、接口边界（模块间解耦）、目标机数据与文档记录的一致性、对 `docs/`（人类）
  与 `.agents/docs/`（agent）的同步更新。
- 合入 `main` 前确保：主机测试通过、交叉构建干净、文档与实现一致。
