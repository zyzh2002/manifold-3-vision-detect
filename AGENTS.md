# AGENTS.md

Instructions for AI coding agents working on this repository.

## Mandatory Skill Invocation

- **ZERO TOLERANCE: You MUST invoke relevant skills via the `skill` tool BEFORE doing anything else - before replying, before exploring files, before asking clarifying questions.**
- This rule overrides any tendency to "just answer quickly" or "just check one thing first."
- Check the available skills list first. If even one skill might apply (brainstorming, systematic-debugging, test-driven-development, writing-plans, psdk-deepwiki-research, etc.), invoke it.
- If you are unsure whether a skill applies, invoke it anyway. You can always stop following it if it turns out irrelevant.
- Failure to invoke applicable skills is a protocol violation.

## Language & Style

- Use English for internal reasoning and Chinese for all user-facing communication.
- **All code comments and documentation must be in English.**
- Chinese characters are forbidden in source files, comments, commit messages, and agent-facing docs.
- The only exception is `README.md` which may contain Chinese for end-user readability.

## Commands

Build commands for Phase 2 and beyond:

```bash
# Cross-compile for Manifold 3 and run static ELF verification
cmake --preset manifold3-cross-release
cmake --build --preset manifold3-cross-release

# Build, deploy to Manifold 3, and run the application in the foreground
./scripts/deploy.sh 192.168.42.120 run
# Skip rebuild and only redeploy + run
./scripts/deploy.sh 192.168.42.120 --no-build run

# Reserved host build for future unit tests
cmake --preset host-debug
cmake --build --preset host-debug

# Future commands (not yet implemented):
#   ./scripts/package_dpk.sh            (Phase 6)
```

## Skills Management

The vendored Superpowers skills under `.agents/skills/` (everything except the
project-owned `psdk-deepwiki-research/`) are installed and maintained with the
`npx skills` CLI (Vercel Labs). `skills-lock.json` is the lock file, with the
same semantics as npm `package-lock.json`: it records exact upstream hashes so
skill versions are reproducible across machines and branches.

| Command | Purpose |
|---|---|
| `npx skills check` | Compare local hashes against upstream and list available updates |
| `npx skills add <owner>/<repo>` | Install/upgrade a skill and update the lock file |
| `npx skills update` | Update all skills and the lock file |
| `npx skills experimental_install` | Restore skills strictly per the lock file (`npm ci` semantics) |

Rules:

- Only the CLI writes `skills-lock.json`; never hand-edit it. Hand-editing
  breaks reproducibility, the same way a hand-edited `package-lock.json` does.
- Run `npx skills check` before upgrading so updates are applied deliberately,
  not blindly.
- Do not install skills by hand (git clone / copy). A manual install makes the
  lock file and the actual install disagree.
- `npx skills rm` does not update the lock file; removing a skill leaves a
  stale entry that `experimental_install` will restore.
- `psdk-deepwiki-research/` is project-owned and intentionally not managed by
  the CLI; do not install or remove it with `npx skills`.

## Manifold 3 Device Access

The Matrice 4E is connected to the development host via USB cable. The onboard
Manifold 3 computer exposes a fixed network address and accepts SSH with the
key stored in the repository.

Connection details:

| Property | Value |
|---|---|
| Host / IP | `192.168.42.120` |
| SSH user | `dji` |
| SSH port | `22` |
| Private key | `config/manifold3_id_rsa` (RSA 4096, committed to repo) |

SSH one-liner:

```bash
ssh -i config/manifold3_id_rsa -o StrictHostKeyChecking=no dji@192.168.42.120
```

SCP one-liner:

```bash
scp -i config/manifold3_id_rsa -o StrictHostKeyChecking=no <local-file> dji@192.168.42.120:<remote-path>
```

Common target inspection commands:

```bash
# Kernel and architecture
ssh -i config/manifold3_id_rsa dji@192.168.42.120 "uname -a"

# Jetson Linux release
ssh -i config/manifold3_id_rsa dji@192.168.42.120 "cat /etc/nv_tegra_release"

# glibc version
ssh -i config/manifold3_id_rsa dji@192.168.42.120 "ldd --version"

# Dynamic dependency check of a deployed binary
ssh -i config/manifold3_id_rsa dji@192.168.42.120 "ldd /path/to/binary"
```

The key file permissions must remain `600`. Do not wrap the key or regenerate it.
The device is physically isolated and the key is non-sensitive for this lab
environment.

Git tracks the key as mode `100644` (it only records the executable bit, never
`0600`), so a fresh clone restores the file as `644`. OpenSSH refuses to use a
group- or world-readable private key, so after cloning run once:

```bash
chmod 600 config/manifold3_id_rsa
```

This key is for local lab use only and is not a leak risk; no rotation,
allowlisting, or remote-exposure mitigation is required.

## Cross-Compilation Toolchain

- The target baseline is Manifold 3 with NVIDIA JetPack 5.1.3 / Jetson Linux r35.5.0, Linux kernel 5.10, and glibc 2.31.
- The primary cross-compiler is NVIDIA's prebuilt Bootlin **GCC 9.3.0 + binutils 2.33.1 + glibc 2.31** AArch64 toolchain.
- The Phase 2 base sysroot is assembled from the Jetson Linux r35.5.0 BSP, Tegra sample root filesystem, and NVIDIA binary overlay. It provides glibc, C/C++ development files, and Tegra runtime libraries. CUDA/TensorRT/cuDNN development packages are deferred to Phase 5.
- The local sysroot may live under `sysroot/`. The directory is ignored by Git and must never be committed.
- Do not use the Bootlin toolchain's base sysroot as the complete application sysroot.
- Do not mix packages or libraries from different JetPack or Jetson Linux releases.
- Do not resolve target headers or libraries from host x86_64 directories.
- crosstool-ng is a fallback that requires a documented, concrete limitation of the NVIDIA toolchain. Do not add or build a crosstool-ng configuration preemptively.
- `-static-libstdc++ -static-libgcc` is not the default. Evaluate it only after target testing demonstrates a real `GLIBCXX_*` compatibility problem.
- Production binaries must pass target-device ELF, symbol-version, dynamic-dependency, and runtime checks.

## Build Configuration Variables

| Variable | Description |
|---|---|
| `MANIFOLD3_TOOLCHAIN_DIR` | Path to the NVIDIA Bootlin GCC 9.3.0 AArch64 toolchain |
| `MANIFOLD3_SYSROOT` | Path to the Jetson Linux r35.5.0 target sysroot; may point to the ignored repository-local `sysroot/` directory |
| `MANIFOLD3_ALLOW_UNVERIFIED_SYSROOT` | CMake cache option for an exceptional, measured, and documented sysroot identity mismatch; defaults to `OFF` |

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

## Phase 2 Progress

Phase 2 is complete, including target-side validation on Manifold 3.

What exists:
- Directory structure
- CMake cross-compilation toolchain (`cmake/aarch64-manifold3.cmake`)
- `CMakePresets.json` for cross and host build presets
- Toolchain smoke tests under `tests/toolchain/`
- `.clangd` LSP configuration
- PSDK git submodule pinned to `3.16.0`
- `scripts/setup_env.sh` for exporting the cross-build environment variables

What is verified on the host:
- NVIDIA Bootlin GCC 9.3.0 toolchain (SHA256: `7725b460...`)
- Jetson Linux r35.5.0 sysroot (BSP + sample rootfs + `apply_binaries.sh`)
- C and C++ cross-compilation produces AArch64 ELF executables
- ELF static checks: architecture, dynamic interpreter, dependencies, GLIBC/GLIBCXX versions
- clangd reports no errors on existing source files with the cross-compile compilation database

What is verified on Manifold 3 (target):
- C and C++ smoke binaries run successfully
- Target environment matches the baseline: Jetson Linux R35.5.0, kernel 5.10.192-tegra,
  glibc 2.31, CUDA 11.4.19, TensorRT 8.5.2, cuDNN 8.6.0
- Dynamic dependencies resolve against the device libraries; GLIBC_2.17 requirements are satisfied
- No sysroot overlay is required

Phase 3 is complete: the PSDK platform layer lives in `src/platform/`, and the minimal PSDK lifecycle and
DPK application are implemented and validated on Manifold 3. The DPK install/start/stop/update/uninstall
lifecycle verification is deferred to Phase 6: it requires the DJI Pilot 2 developer workflow and real DJI
developer credentials, which are unavailable in Phase 3. Development iteration uses `scripts/deploy.sh`
direct deployment instead.

## Phase 4 / 5A Progress

Phase 4 (single-stream capture) and Phase 5A (synthetic TensorRT inference pipeline) are complete and
target-validated on Manifold 3. Phase 5B (real YOLO11-seg model) is pending.

What exists:

- `src/capture/` — `LiveviewCapture` (PSDK ImageStream NV12) plus `LatestFrameSlot`: validated NV12 frame
  handoff with separate source-drop / handoff-drop / invalid-frame counters, condition-variable wakeup,
  and clean Stop. Callback buffers are never retained.
- `src/inference/` — CPU `PreprocessNv12ToNchw`, `TensorRtEngine` (loads ONLY the synthetic three-output
  ABI in `synthetic_engine_contract.h`, by-name mode/dtype/shape validation, persistent CUDA buffers,
  event-based stage timing), `DecodeSyntheticSeg` (exact-size-checked YOLO11-seg-shape decode),
  `PipelineWindowStats` (per-window nearest-rank avg/p95/max per stage).
- `src/core/psdk_user_info.{h,cpp}` — credential filling with correct fixed-field vs NUL-terminated
  string handling, unit-tested.
- `scripts/extend_sysroot_from_device.sh` — copies CUDA/TensorRT/cuDNN dev files from a connected device
  into a staging dir, restores the exact device symlink layout, then installs; hard-fails on package
  version mismatch. `scripts/check_inference_sysroot.sh --sysroot <path>` verifies links, SONAMEs, and
  AArch64 ELF. Host tests use fake ssh/scp under `tests/scripts/`.
- `scripts/configure_cross_with_credentials.sh` — injects credentials from git-ignored
  `.local/credentials.env` (mode 600, never committed) into the cross configure; falls back to
  placeholders when absent.
- `scripts/generate_dummy_onnx.py` — generates the synthetic test ONNX (run via `uv run --with onnx`).
- `scripts/run_inference_smoke.sh` — target-side synthetic engine smoke test.
- Host unit tests: `tests/core/`, `tests/capture/`, `tests/inference/`, `tests/scripts/` (run with the
  host-debug preset + ctest); cross ELF checks in `tests/toolchain/` (CXX set includes `libm.so.6`
  because the device-derived sysroot libstdc++ carries that DT_NEEDED).

Target validation (synthetic engine):

- NV12 1440x1080 @ ~30 fps; per-window end-to-end avg ~26 ms, preprocess ~12.8 ms, engine ~12.2 ms,
  postprocess ~0.15 ms; invalid frames 0; RSS stable (~1.9 MB/min creep attributed to driver/SDK lazy
  allocation, no app leak path); clean SIGTERM shutdown.
- These figures are synthetic-engine-only and do not predict real-model performance.

What is deferred (Phase 5B, real model):

- Freeze the real YOLO11-seg ABI (standard 2-output or custom multi-task contract); match PC/device
  preprocessing; inverse-letterbox geometry; source-frame instance masks; numeric comparison against
  ONNX Runtime; real-model latency/throughput/memory/drop measurement. See `docs/plan.md` Phase 5B and
  `docs/superpowers/specs/2026-07-31-tree-crown-age-design.md`.

Device notes:

- Before running the app on Manifold 3, stop the onboard `Smart3DExplore` application
  (`dji_app_ctl stop Smart3DExplore`; fall back to `pkill -f Smart3DExplore`), otherwise the local
  channel bind fails with "Address already in use". Restore with `dji_app_ctl start Smart3DExplore`.
