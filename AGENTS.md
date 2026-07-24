# AGENTS.md

Instructions for AI coding agents working on this repository.

## Language & Style

- Use English for internal reasoning and Chinese for all user-facing communication.
- **All code comments and documentation must be in English.**
- Chinese characters are forbidden in source files, comments, commit messages, and agent-facing docs.
- The only exception is `README.md` which may contain Chinese for end-user readability.

## Commands

Build commands are not yet available in Phase 1. They will be added in Phase 2 as `scripts/` is populated.

Future expected commands:

```bash
# Cross-compile for Manifold 3 (host -> aarch64)
./scripts/build.sh

# Host build for unit tests
cmake -B build-host -DCMAKE_BUILD_TYPE=Debug && cmake --build build-host

# Deploy to Manifold 3 via SCP
./scripts/deploy.sh <manifold3-ip>

# Package a release DPK (Phase 6)
./scripts/package_dpk.sh
```

## Cross-Compilation Toolchain

- The target baseline is Manifold 3 with NVIDIA JetPack 5.1.3 / Jetson Linux r35.5.0, Linux kernel 5.10, and glibc 2.31.
- The primary cross-compiler is NVIDIA's prebuilt Bootlin **GCC 9.3.0 + binutils 2.33.1 + glibc 2.31** AArch64 toolchain.
- The complete target sysroot is assembled from the Jetson Linux r35.5.0 BSP, Tegra sample root filesystem, and matching JetPack 5.1.3 AArch64 development packages.
- The local sysroot may live under `sysroot/`. The directory is ignored by Git and must never be committed.
- Do not use the Bootlin toolchain's base sysroot as the complete application sysroot.
- Do not mix packages or libraries from different JetPack or Jetson Linux releases.
- Do not resolve target headers or libraries from host x86_64 directories.
- crosstool-ng is a fallback that requires a documented, concrete limitation of the NVIDIA toolchain. Do not add or build a crosstool-ng configuration preemptively.
- `-static-libstdc++ -static-libgcc` is not the default. Evaluate it only after target testing demonstrates a real `GLIBCXX_*` compatibility problem.
- Production binaries must pass target-device ELF, symbol-version, dynamic-dependency, and runtime checks.

## Environment Variables

| Variable | Description |
|---|---|
| `MANIFOLD3_TOOLCHAIN_DIR` | Path to the NVIDIA Bootlin GCC 9.3.0 AArch64 toolchain |
| `MANIFOLD3_SYSROOT` | Path to the complete Jetson Linux r35.5.0 target sysroot; may point to the ignored repository-local `sysroot/` directory |

## PSDK Submodule

- The PSDK lives at `third_party/psdk/` as a git submodule pinned to tag `3.16.0`.
- **Do NOT modify files inside `third_party/psdk/`.** This is a vendored read-only dependency.
- Platform adaptation code should combine Linux common sources from `third_party/psdk/samples/sample_c/platform/linux/common/` with the Manifold 3-specific USB Bulk HAL from `third_party/psdk/samples/sample_c/platform/linux/manifold3/hal/`, then be adapted under `src/platform/`.
- PSDK static libraries are at `third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/`.
- PSDK headers are at `third_party/psdk/psdk_lib/include/`.

## PSDK API Research

- **Before writing any code that uses PSDK APIs, you MUST consult the DeepWiki documentation via the `psdk-deepwiki-research` skill.** Perform the research in the main conversation by default.
- **Research flow (defined in the skill, do NOT skip or reorder):**
  1. Read the wiki structure via the DeepWiki MCP tool.
  2. Run `python3 .agents/skills/psdk-deepwiki-research/scripts/wiki-cache.py pull --force` (mandatory; `--force` is required, exit code 2 is not success).
  3. Verify the cache with `info --json` / `ls --json`, then read every relevant chapter in full.
  4. Cross-check the DeepWiki conclusions against local headers and samples under `third_party/psdk/`.
  5. Only after the above, use `ask_question` to resolve any specific remaining ambiguity.
  Never call `ask_question` first.
- **Authoritative source rules (conflict resolution):**
  - Use **DeepWiki** for architecture, usage patterns, callback timing, and lifecycle behavior.
  - Use **local PSDK 3.16.0 headers** at `third_party/psdk/psdk_lib/include/` and the samples under `third_party/psdk/samples/` as the authority for version-specific facts: function signatures, enum values, struct layouts, error codes, and platform macros.
  - When the two disagree, local headers win. Record such discrepancies in the commit message or design note; do not silently blend them into a guessed conclusion.
- The skill requires current, validated access through the DeepWiki MCP service. Do not substitute browser, web-fetch, search, or other documentation transports when that service is unavailable.
- Reference chapters (slugs shown for orientation only; resolve the **exact chapter name** via `wiki-cache.py ls --json` before calling `get`):
  - `1-overview` / `1.2-architecture-overview`: overall architecture
  - `3.4-live-video-streaming`: liveview API
  - `2.1-linux-aarch64-platform`: aarch64 platform specifics
  - `5.1-os-abstraction-layer-(osal)`: OSAL porting
- Do NOT guess PSDK function signatures, enum values, or callback semantics.

## General Web Research (non-PSDK)

- Exa MCP and generic web search may be used for **general engineering research** such as toolchain (gcc, crosstool-ng), JetPack, Linux kernel, glibc, and third-party libraries.
- They **must not** be used for PSDK API facts. All PSDK research is governed by the `psdk-deepwiki-research` skill and the local headers under `third_party/psdk/psdk_lib/include/`.

## Code Style

- **Language**: C++17 for application code (`src/app/`, `src/capture/`, `src/inference/`). C99 for platform port (`src/platform/`).
- **Format**: LLVM-style clang-format, 120 column limit.
- **Naming**: snake_case for files, PascalCase for types, snake_case for functions and variables.
- **No Chinese comments**. No non-English identifier names.
- PSDK C headers carry `extern "C"` guards; no extra wrapping is needed when including from C++.

## Git Workflow

- Commit messages in English, prefix with conventional commits: `chore:`, `feat:`, `fix:`, `docs:`.
- Rebase over merge for linear history.
- Before committing, verify: `git submodule update --init --recursive`.

### Development Branch Policy

- Use a lightweight trunk-based workflow. `main` must remain buildable and verifiable.
- For normal implementation work, update `main` and create one short-lived branch per objective.
- Use one of these branch prefixes: `feat/`, `fix/`, `docs/`, `chore/`, `refactor/`, or `test/`.
- Use a short kebab-case topic after the prefix, for example `feat/liveview-capture` or `docs/build-environment`.
- Keep each branch focused on one reviewable objective. Do not mix unrelated cleanup or features.
- Do not create long-lived `develop`, `integration`, `release`, or agent-specific branches.
- Rebase the development branch onto the latest `main` before integration; do not create merge commits.
- Agents may create local branches and commits as needed for an approved task.
- Agents must obtain explicit user approval before pushing, creating a pull request, merging into `main`, or deleting a remote branch.
- Direct commits to `main` require explicit user authorization for the specific task.
- Delete short-lived development branches after their changes are integrated.

## Phase 1 Scope

Phase 1 is scaffolding only. No source code. No CMake files. No scripts. No config files.

What exists:
- Directory structure
- `.gitkeep` placeholders
- `.gitignore`
- `README.md`, `AGENTS.md`, `docs/plan.md`, `docs/architecture.md`, `docs/build-environment.md`
- `toolchain/README.md`
- PSDK git submodule pinned to `3.16.0`

What to do next (Phase 2):
1. Download the NVIDIA Bootlin GCC 9.3.0 toolchain.
2. Assemble the complete Jetson Linux r35.5.0 sysroot under `sysroot/` or another `MANIFOLD3_SYSROOT` location.
3. Write the minimal CMake cross-compilation configuration and validate an AArch64 target program.
4. Port the required PSDK platform layer into `src/platform/`.
5. Implement the minimal PSDK lifecycle and DPK application before adding video capture.
6. Validate one visible-light NV12 ImageStream before selecting detailed capture abstractions.
