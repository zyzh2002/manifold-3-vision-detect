# Phase A Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden `feat/liveview-capture` into a safely mergeable synthetic TensorRT inference pipeline milestone.

**Architecture:** Fix the final-review findings in order: PSDK credential boundary (A1), reproducible device-derived sysroot extension (A2-A3), safe NV12 frame handoff with drop accounting (A4), enforced synthetic engine contract (A5), honest per-window metrics (A6), narrowed documentation claims (A7), full verification (A8), target regression (A9), final review (A10).

**Tech Stack:** C++17, CMake (host-debug + manifold3-cross-release), TensorRT 8.5.2, bash (fake-ssh/scp script tests), uv for ONNX.

## Global Constraints

- C++17; LLVM clang-format 120; snake_case files/functions/variables; PascalCase types; no Chinese in code/comments/docs (except README.md).
- PSDK 3.16.0 local headers/samples are version authority. Manifold 3: JetPack 5.1.3, r35.5.0, CUDA 11.4, TensorRT 8.5.2.
- Host build must keep working; cross build never resolves host x86_64 paths.
- Real credentials only via CMake cache; never commit. `third_party/psdk/` read-only. `sysroot/` git-ignored.
- This phase fixes only the synthetic dummy ABI; real-model ABI stays pending (Plan B, separate branch).

## Reference

- Findings source: `.superpowers/sdd/final-review-findings.md` (git-ignored working note).
- Branch: `feat/liveview-capture`; base `a07d337`; current HEAD `776c9ef`; working tree clean.

---

### Task 0: Baseline

Recorded: host tests 2/2 PASS; cross build + AArch64 ELF 2/2 PASS; credential scan clean; working tree clean. No commit.

### Task 1: PSDK credential boundary fix

- Create `src/core/psdk_user_info.h/.cpp` with `struct PsdkCredentialStrings` and
  `bool FillPsdkUserInfo(const PsdkCredentialStrings&, T_DjiUserInfo*)`.
- String fields (appName[32], developerAccount[64]) require NUL: validate `strlen < sizeof`, copy `strncpy(..., sizeof-1)`.
- Fixed fields (appId[16], appKey[32], appLicense[512], baudRate[7]) allow full length: validate `strlen <= sizeof`, copy `memcpy(..., strlen)`.
- Placeholder check (`your_app_*`) kept. memset struct first. Null-pointer arguments fail.
- Tests `tests/core/test_psdk_user_info.cpp`: full-length 16/32/512/7 copies preserve last byte; 31/63 string fields NUL-terminated; over-limit +1 fails; placeholders fail; nulls fail.
- Wire into `psdk_lifecycle.cpp` (replace inline FillUserInfo), `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` + `tests/core/CMakeLists.txt`.
- TDD: RED build fails missing header; GREEN ctest PASS.
- Commit: `fix: preserve full-length fixed PSDK credential fields`

### Task 2: sysroot script exact package baseline

- `scripts/extend_sysroot_from_device.sh`: replace prefix `dpkg-query` with exact per-package checks:
  cuda-cudart-11-4, cuda-cudart-dev-11-4, libcudla-11-4, libcudla-dev-11-4, libcublas-11-4,
  libcublas-dev-11-4, libcudnn8, libcudnn8-dev, libnvinfer8, libnvinfer-dev,
  libnvinfer-plugin8, libnvinfer-plugin-dev, libnvonnxparsers8, libnvonnxparsers-dev
  (confirm exact names/versions on device read-only before freezing; versions in docs/build-environment.md).
- Missing package or version mismatch: `ERROR` + exit 1 before any scp. No `--allow-version-mismatch`.
- Argument parsing: `--sysroot` without value -> usage + exit 2; second positional -> exit 2; unknown flag -> exit 2; absolute-path SYSROOT.
- Host tests with fake ssh/scp (`tests/scripts/fixtures/`): happy path (all versions correct, scp after checks), missing package exits 1 with no scp, mismatch exits 1 with no scp, argument errors exit 2.
- Commit: `fix: enforce the documented phase 5 package baseline`

### Task 3: staged sysroot install

- Copy into `mktemp -d` staging on the same filesystem first; trap cleanup EXIT/INT/TERM.
- Managed scope only: NvInfer*.h, NvOnnx*.h, `usr/local/cuda/include/` tree, and the .so families
  (libnvinfer/libnvonnxparser/libnvinfer_plugin/libcudnn.so.8/libcudart/libcudla/libcublas/libcublasLt).
- Verify real files: regular, non-empty, AArch64 ELF, correct SONAME; restore exact device symlink chains in staging; install; verify target sysroot with checker `--sysroot`.
- `scripts/check_inference_sysroot.sh`: support `--sysroot <path>` (priority: `--sysroot` > `$MANIFOLD3_SYSROOT` > repo/sysroot); check `-L` + exact readlink targets; `libcudnn.so` must NOT exist (use `[ -e ] || [ -L ]` to remove dangling); ELF/SONAME checks; full link closure.
- Final line output documented as `PASS: ...` then `DONE: sysroot extension applied to <abs>`.
- Host tests: empty explicit sysroot fails even when default is complete; plain file instead of symlink fails; wrong readlink fails; dangling link fails; rerun idempotent for managed files.
- Device verification on a disposable copy (e.g. `cp -a --reflink=auto sysroot /tmp/opencode/...`), never the working sysroot.
- Commit: `fix: install the device-derived sysroot extension atomically`

### Task 4: safe NV12 frame slot

- Create `src/capture/latest_frame_slot.h/.cpp`: `OwnedNv12Frame{data,width,height,frame_id}`,
  `enum class FramePushResult{kStored,kReplaced,kInvalid}`,
  `LatestFrameSlot::Push/WaitTake/Stop`, `replaced_frames()`, `invalid_frames()`.
- `IsValidNv12Frame`: null data, zero/odd dims, `expected = w*h*3/2 <= UINT32_MAX`, `len == expected`.
- `LiveviewCapture::OnImage` validates `pixFmt == PIXFMT_NV12` too; invalid -> invalid_frames++ and no push.
- Stats split: `source_dropped_frames` (frameId gaps), `handoff_dropped_frames` (replaced), `invalid_frames`.
- Locking: frame slot has own mutex+cv; stats separate mutex; never nested; shutdown calls `Stop()` to wake waiters.
- `main.cpp` uses `WaitTake` (no 5ms/20ms polling).
- Tests `tests/capture/test_latest_frame_slot.cpp`: valid push/take, null/odd/short/long fails, replace semantics + counter, timeout, wake on push, wake on stop, 1000 overwrites keep one frame.
- Commit: `fix: validate NV12 callbacks and account for handoff drops`

### Task 5: enforced synthetic engine contract

- Create `src/inference/synthetic_engine_contract.h`: tensor names (images/output0/output1/output2),
  shapes (input 1x3x1280x1280; output0 float[1,43,25600]; output1 float[1,32,25600]; output2 float[1,32,160,160]).
- `TensorRtEngine::Load`: verify by NAME (not enumeration order) mode/dtype/shape; reject dynamic/negative dims; allocate persistent CUDA buffers + stream once; setTensorAddress once; on any failure free everything, loaded=false.
- `TensorRtEngine::Infer(input, SyntheticOutputs*, EngineTiming*)`: persistent buffers, CUDA events for h2d/execute/d2h/total; check every CUDA return; null checks.
- TensorRT object destruction: use `delete` if 8.5 header allows (removes deprecated destroy()); else keep destroy() with scoped pragma.
- `DecodeSyntheticSeg(outputs, detections)`: exact size validation before decode; false on mismatch.
- smoke prints tensor names/shapes/dtype and timings.
- Tests: size mismatches fail; wrong dtype/shape Load fails; name-order independence; correct path passes.
- Commit: `fix: enforce the synthetic TensorRT engine contract`

### Task 6: per-window metrics

- `src/inference/pipeline_metrics.h/.cpp`: `LatencySamples{average_us, percentile_us, max_us, clear}` (nearest-rank percentile `ceil(p*N)-1`, clamped); `PipelineWindowStats{frames, detections, preprocess, h2d, execute, d2h, engine_total, postprocess, end_to_end}`.
- `main.cpp`: one loop iteration = WaitTake -> e2e start -> preprocess -> Infer (timing) -> Decode -> e2e end; rollover at 1s prints all window values and clears all counters.
- Log single line: `pipeline synthetic=true fps=... frames=... detections=... pre_avg/p95 ... e2e_avg/p95/max ... source_drop=... handoff_drop=... invalid=... rss_kb=...`.
- Tests `tests/inference/test_pipeline_metrics.cpp`: empty, 1/2/20 samples p95, clear, rollover, max/avg isolation.
- Commit: `fix: report honest per-window inference pipeline metrics`

### Task 7: narrow documentation claims

- `docs/plan.md`: Phase 5 split into 5A Synthetic Device Pipeline [DONE] and 5B Real Model [PENDING]; validation record states 21fps/1.4ms are synthetic-only; dets=0 does not validate detection correctness.
- Spec/plan docs: replace "real YOLO11-seg output shapes"/"exact postprocessing shapes"/"swap the engine" with "synthetic three-output test contract; real-model contract pending"; fix `160*160=25600` comment; remove trailing whitespace.
- `docs/build-environment.md`: hard-fail on version mismatch; staging install; checker symlink/SONAME checks; final `DONE` line; idempotence scoped to managed files.
- Commit: `docs: align phase 5 claims with the synthetic inference milestone`

### Task 8: full verification

- host: build + ctest all (psdk_user_info, latest_frame_slot, inference_preprocess, inference_postprocess, synthetic_engine_contract, pipeline_metrics, sysroot_extend_script, sysroot_checker).
- cross: build clean, no new warnings (deprecated destroy handled in A5), ELF 2/2, app + smoke built.
- `git diff --check`, `bash -n scripts/*.sh`, clang-format dry-run where available.
- ONNX regen: `uv run --with onnx python3 scripts/generate_dummy_onnx.py --out build/synthetic-review.onnx`; if PyPI unreachable, use uv cache; else record external blocker (do not claim regeneration passed).
- Checker: `--sysroot` explicit passes; empty dir fails non-zero.

### Task 9: target regression

- Stop Smart3DExplore; run synthetic smoke (`scripts/run_inference_smoke.sh`); deploy app with dummy engine, run >=10 min; record fps, drops, invalid, stage latencies, e2e, RSS start/end; SIGTERM clean shutdown; restore Smart3DExplore (record result).
- Commit: `docs: record hardened dummy inference pipeline validation`

### Task 10: final review and branch close

- Whole-branch review base..HEAD (Critical/Important = 0); credential scan; host/cross/target evidence; then offer merge decision.

## Self-Review

- A1-A10 map 1:1 to final-review-findings.md blockers B1-B4 and importants I1-I6 (A1=B3, A2=B1/I5, A3=B2/I6, A4=I1/I2, A5=B4/I4, A6=I3, A7=docs scope, A8/A9 verification, A10 gate).
- Real-model items (R1-R6) are explicitly deferred to Plan B — stated in A5/A7 so no silent claim of real-model support.
- Type consistency: OwnedNv12Frame/FramePushResult used by both liveview_capture and main; SyntheticOutputs/EngineTiming used by engine + main + smoke; names frozen in A5 and referenced unchanged later.
