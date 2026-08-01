# Build Environment

## Selected Build Baseline

The following baselines are used for host-side cross-compilation. They are confirmed to
work with the toolchain and sysroot assembled in this repository. Target runtime
confirmation requires Manifold 3 hardware.

| Component | Baseline |
|---|---|
| Target | DJI Manifold 3, AArch64 GNU/Linux |
| Manifold 3 SDK base | NVIDIA JetPack 5.1.3 |
| Jetson Linux baseline | r35.5.0 |
| Linux kernel baseline | 5.10 |
| Target glibc baseline | 2.31 |
| Primary compiler | NVIDIA Bootlin GCC 9.3.0 binary toolchain |
| Binutils | 2.33.1 |
| CUDA | 11.4.19 |
| TensorRT | 8.5.2 |
| cuDNN | 8.6.0 |
| DJI Payload SDK | 3.16.0 |

The NVIDIA Bootlin toolchain is the primary compiler because NVIDIA specifies it for applications targeting Jetson
Linux r35.5.0. The project does not build a compiler toolchain by default.

### Baseline Assumptions

The JetPack 5.1.3 / Jetson Linux r35.5.0 baseline is a reasonable starting point for Manifold 3 because:

- Manifold 3 is built on NVIDIA Jetson Orin NX;
- the PSDK 3.16.0 `libpayloadsdk.a` is built with `aarch64-linux-gnu-gcc` for AArch64 glibc targets;
- JetPack 5.x is the production release family for Orin NX with GCC 9.3 + glibc 2.31 + CUDA 11.4 + TensorRT 8.5.

The exact JetPack / L4T version shipped in Manifold 3 firmware is not published by DJI. The actual
device firmware is the runtime authority. Confirm the exact release with `cat /etc/nv_tegra_release`
on the target and adjust the sysroot baseline if a measured mismatch is found.

## Toolchain Policy

- Download and use NVIDIA's prebuilt Bootlin GCC 9.3.0 AArch64 toolchain.
- Keep the toolchain outside version control. It may be installed globally or under the ignored `.local-toolchains/`
  directory.
- Use crosstool-ng only after a concrete limitation of the NVIDIA toolchain has been demonstrated and documented.
- The initial inference implementation will call TensorRT and CUDA C/C++ APIs without compiling custom `.cu` files.

## Environment Setup

Two environment variables control the cross-build:

| Variable | Purpose |
|---|---|
| `MANIFOLD3_TOOLCHAIN_DIR` | Path to the Bootlin GCC 9.3.0 aarch64 toolchain (containing `bin/`, `aarch64-linux/`, etc.) |
| `MANIFOLD3_SYSROOT` | Path to the Jetson Linux r35.5.0 target sysroot |

### Quick setup (local to this repository)

```bash
export MANIFOLD3_TOOLCHAIN_DIR="$(git rev-parse --show-toplevel)/.local-toolchains/bootlin-gcc-9.3-nvidia"
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

Or source the repository helper script, which applies the same defaults and preserves
any pre-existing values:

```bash
source scripts/setup_env.sh
```

The `sysroot/` symlink at the repository root points to the assembled rootfs under `.local-toolchains/`.
Both `.local-toolchains/` and `sysroot/` are gitignored.

### CMake Presets and IDE Integration

The project provides `CMakePresets.json` with `manifold3-cross-release` and `host-debug`
presets. CMake Tools (VSCode), CLion, and the CMake CLI all support presets natively.

Environment variables `MANIFOLD3_TOOLCHAIN_DIR` and `MANIFOLD3_SYSROOT` must be set before using the
`manifold3-cross-release` preset or an IDE session that reads `build-cross/compile_commands.json`. The reserved
`host-debug` preset does not require them.

The repository `.clangd` configuration reads `build-cross/compile_commands.json`. Because `build-cross/` is
gitignored, run `cmake --preset manifold3-cross-release` once after a fresh clone before expecting clangd
diagnostics or code navigation.

### Global toolchain install (optional)

If you prefer a system-wide toolchain:

```bash
export MANIFOLD3_TOOLCHAIN_DIR=/opt/toolchains/bootlin-gcc-9.3
export MANIFOLD3_SYSROOT="/opt/sysroots/manifold3-r35.5.0"
```

### Verification

```bash
# Toolchain
"$MANIFOLD3_TOOLCHAIN_DIR/bin/aarch64-linux-gcc" --version
# Expected: aarch64-linux-gcc.br_real (Buildroot ...) 9.3.0

# Sysroot
test -f "$MANIFOLD3_SYSROOT/lib/aarch64-linux-gnu/libc.so.6" && echo "glibc OK"
test -f "$MANIFOLD3_SYSROOT/usr/lib/aarch64-linux-gnu/tegra/libcuda.so" && echo "Tegra libs OK"

# Cross-compile
cmake --preset manifold3-cross-release
cmake --build --preset manifold3-cross-release
# Expected: both AArch64 smoke binaries compile and verify_toolchain_smoke passes 2/2 ELF tests
```

## Prerequisites

The following host packages are required before assembling the toolchain and sysroot:

| Package | Used by |
|---|---|
| `bzip2` | Extracting .tbz2 archives |
| `lbzip2` | Parallel bzip2 extraction used by `apply_binaries.sh` |
| `zstd` | Extracting zstd-compressed Debian packages in `apply_binaries.sh` |
| `tar` | Archive extraction |
| `curl` or `wget` | Downloading archives |
| `cmake` >= 3.21 | Building the project |
| `binutils` | Providing `readelf` and other ELF inspection tools |
| `file` | Checking ELF type and target architecture |

Install on Debian/Ubuntu:

```bash
sudo apt install bzip2 lbzip2 zstd tar curl cmake binutils file
```

## Bootlin GCC 9.3.0 aarch64 Toolchain

The toolchain is downloaded from NVIDIA's official Jetson Linux download page. It is a repackaged
Bootlin `aarch64--glibc--stable-2020.08-1` build with NVIDIA-specific wrapper scripts and both
`aarch64-linux-` and `aarch64-buildroot-linux-gnu-` command prefixes.

| Property | Value |
|---|---|
| GCC version | 9.3.0 |
| Binutils version | 2.33.1 |
| glibc version | 2.31 |
| Linux headers | 4.9.234 |
| Download URL | <https://developer.nvidia.com/embedded/jetson-linux/bootlin-toolchain-gcc-93> |
| Download filename | `aarch64--glibc--stable-final.tar.gz` |
| SHA256 | `7725b4603193a9d3751d2715ef242bd16ded46b4e0610c83e76d8891cf580975` |
| Archive size | ~92 MB |
| Command prefix | `aarch64-linux-` |
| Alternative prefix | `aarch64-buildroot-linux-gnu-` (symlinks to wrapper) |

### Quick download & setup

```bash
# From the repository root
mkdir -p .local-toolchains && cd .local-toolchains
curl -L -o bootlin-gcc-9.3-nvidia.tar.gz https://developer.nvidia.com/embedded/jetson-linux/bootlin-toolchain-gcc-93
echo "7725b4603193a9d3751d2715ef242bd16ded46b4e0610c83e76d8891cf580975  bootlin-gcc-9.3-nvidia.tar.gz" | sha256sum -c
mkdir -p bootlin-gcc-9.3-nvidia && tar xzf bootlin-gcc-9.3-nvidia.tar.gz -C bootlin-gcc-9.3-nvidia
cd ..
export MANIFOLD3_TOOLCHAIN_DIR="$(pwd)/.local-toolchains/bootlin-gcc-9.3-nvidia"
```

## Phase 2 Base Sysroot

The Phase 2 base sysroot provides glibc, standard C/C++ development files, and NVIDIA Tegra
runtime libraries. It is assembled from the following official NVIDIA packages:

| Package | Filename | Size | SHA256 |
|---|---|---|---|
| Jetson Linux Driver Package (BSP) | `Jetson_Linux_R35.5.0_aarch64.tbz2` | ~725 MB | `8cde3bd937d3eedb640a1c58d108c109f7cb904c38a03101dc17904b7d185ddf` |
| Tegra Linux Sample Root Filesystem | `Tegra_Linux_Sample-Root-Filesystem_R35.5.0_aarch64.tbz2` | ~1.5 GB | `d61cb54357f44fb7402da7e52151bf73a1cd698e33eaa6e6b49ab5ddc1bf60a6` |

Download base URL: `https://developer.download.nvidia.com/embedded/L4T/r35_Release_v5.0/release/`

The target sysroot is built from the matching NVIDIA release rather than copied from the Bootlin toolchain alone:

```text
Jetson Linux r35.5.0 Driver Package (BSP)
+ Tegra Linux Sample Root Filesystem
+ NVIDIA binary overlay from apply_binaries.sh
```

This Phase 2 base contains glibc, standard Linux development files, and the NVIDIA runtime overlay. CUDA, TensorRT,
cuDNN, OpenCV, FFmpeg, GStreamer, the ONNX parser, and TensorRT plugin development packages are not Phase 2 inputs.
They extend this verified base only in the later phase that uses their APIs.

The Phase 2 base sysroot may be stored at `sysroot/` in the repository working directory. That directory is ignored by Git
and must never be committed. A centrally managed location is also supported. The authoritative path is supplied through:

```bash
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

Do not mix files from other JetPack or Jetson Linux releases into this sysroot. CMake must use `MANIFOLD3_SYSROOT` as
the target search root and must not resolve target headers or libraries from host x86_64 directories.

The standard r35.5.0 sysroot is the build baseline. The actual Manifold 3 firmware remains the runtime authority. Copy a
file from the device only after a measured ABI or dependency difference requires a documented overlay.

### Assembly Steps

```bash
# Working directory: repository root

# 1. Create downloads directory
mkdir -p .local-toolchains/downloads && cd .local-toolchains/downloads

# 2. Extract BSP
tar xjf Jetson_Linux_R35.5.0_aarch64.tbz2

# 3. Extract sample rootfs into Linux_for_Tegra/rootfs
tar xjf Tegra_Linux_Sample-Root-Filesystem_R35.5.0_aarch64.tbz2 -C Linux_for_Tegra/rootfs/

# 4. Apply NVIDIA binary overlays (rootless mode for sysroot-only use)
cd Linux_for_Tegra
./apply_binaries.sh --rootless --target-overlay

# 5. Symlink rootfs as the repo sysroot (from repo root)
cd "$(git rev-parse --show-toplevel)"
ln -sf .local-toolchains/downloads/Linux_for_Tegra/rootfs sysroot

# 6. Relativize absolute .so symlinks in the sysroot
#
# The Ubuntu 20.04 rootfs ships dev symlinks such as libpthread.so ->
# /lib/aarch64-linux-gnu/libpthread.so.0. Cross-linkers do not apply the
# sysroot prefix to absolute symlink targets, fall back to the static archive,
# and then fail on GLIBC_PRIVATE symbols (e.g. `_dl_pagesize` from
# libpthread.a). Since /lib is merged into /usr/lib, fixing the links under
# usr/lib/aarch64-linux-gnu is sufficient.
cd sysroot/usr/lib/aarch64-linux-gnu
for l in lib*.so lib*.so.*; do
    [ -L "$l" ] || continue
    t=$(readlink "$l")
    case "$t" in
        /*) b=$(basename "$t"); [ -e "$b" ] && ln -sfn "$b" "$l";;
    esac
done

# 7. Set environment
export MANIFOLD3_SYSROOT="$(pwd)/sysroot"
```

### Base Sysroot Contents (after apply_binaries.sh)

- glibc 2.31 (`lib/aarch64-linux-gnu/libc-2.31.so`)
- libstdc++ 6.0.28 (`usr/lib/aarch64-linux-gnu/libstdc++.so.6.0.28`)
- NVIDIA Tegra driver libraries (`usr/lib/aarch64-linux-gnu/tegra/`)
- CUDA driver `libcuda.so` (runtime only; CUDA toolkit headers require JetPack SDK)

### JetPack 5.1.3 SDK Components (deferred)

The following packages are added only when their APIs are used:

| Component | Version | Required by |
|---|---|---|
| CUDA Toolkit | 11.4.19 | Phase 5 (TensorRT inference) |
| TensorRT | 8.5.2 | Phase 5 (TensorRT inference) |
| cuDNN | 8.6.0 | Phase 5 (TensorRT inference) |

These are installed via NVIDIA SDK Manager or the JetPack APT repository onto the target sysroot.
They extend the verified Phase 2 base sysroot during Phase 5; they are not required for the Phase 2 smoke binaries.

By default, the toolchain rejects a sysroot that lacks a matching r35.5.0 `/etc/nv_tegra_release`. Pass
`-DMANIFOLD3_ALLOW_UNVERIFIED_SYSROOT=ON` only when target measurements establish and document why a different or
device-derived sysroot is required.

## Phase 5 Sysroot Extension

The Phase 5 inference build requires CUDA Toolkit and TensorRT development files. They are copied from the
Manifold 3 device firmware (the runtime authority) into the sysroot at the same relative paths, so `-I`/`-L`
flags and `#include <cuda_runtime.h>` / `#include <NvInfer.h>` resolve under the sysroot prefix.

### Device packages (recorded 2026-07-31)

The exact packages the script queries on the device (`dji@192.168.42.120`), verified with
`dpkg-query -W -f='${Version}' <pkg>` for each of the 14 names. The script hard-fails before
any copy when a package is missing or its version differs:

| Package | Version | Architecture |
|---|---|---|
| `cuda-cudart-11-4` | 11.4.298-1 | arm64 |
| `cuda-cudart-dev-11-4` | 11.4.298-1 | arm64 |
| `libcudla-11-4` | 11.4.298-1 | arm64 |
| `libcudla-dev-11-4` | 11.4.298-1 | arm64 |
| `libcublas-11-4` | 11.6.6.84-1 | arm64 |
| `libcublas-dev-11-4` | 11.6.6.84-1 | arm64 |
| `libcudnn8` | 8.6.0.166-1+cuda11.4 | arm64 |
| `libcudnn8-dev` | 8.6.0.166-1+cuda11.4 | arm64 |
| `libnvinfer8` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvinfer-dev` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvinfer-plugin8` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvinfer-plugin-dev` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvonnxparsers8` | 8.5.2-1+cuda11.4 | arm64 |
| `libnvonnxparsers-dev` | 8.5.2-1+cuda11.4 | arm64 |

These match the JetPack 5.1.3 baseline (CUDA 11.4, TensorRT 8.5.2) recorded in "Selected Build Baseline".

### Assembly Steps (from a connected device)

The Manifold 3 firmware is the runtime authority, so the Phase 5 extension is copied from the device. The
`scripts/extend_sysroot_from_device.sh` script performs the whole procedure: it checks device reachability and
package versions, copies every header and library, and restores the device symlink layout.

The copy is staged before anything touches the sysroot: the script creates a staging directory with `mktemp -d`
inside the sysroot (same filesystem, so the final install is a rename), a cleanup trap removes it on EXIT, INT,
and TERM, and the staged tree is verified with `scripts/check_inference_sysroot.sh` before installation. A failed
or interrupted run therefore never leaves a partial extension in the sysroot; only the verified staged tree is
installed, replacing exactly the managed files listed below.

```bash
# Preconditions
# 1. Manifold 3 connected via USB; network up (fixed address, see AGENTS.md).
# 2. SSH key permissions: chmod 600 config/manifold3_id_rsa
# 3. Device package versions match the table above (script hard-fails on mismatch).

scripts/extend_sysroot_from_device.sh              # 192.168.42.120, $MANIFOLD3_SYSROOT or ./sysroot
scripts/extend_sysroot_from_device.sh 10.0.0.5      # other address
scripts/extend_sysroot_from_device.sh --sysroot /opt/m3-sysroot   # explicit sysroot
# Expected final lines:
#   PASS: inference sysroot extension present
#   DONE: sysroot extension applied to /opt/m3-sysroot
```

The script is the primary path; the file table below and the symlink layout that follows document exactly what it
copies and how it links, so a partial failure can be repaired by hand. If the device is unavailable, the same
`.deb` packages (versions in the table above) can be extracted with `dpkg-deb -x` into the sysroot; the symlink
restoration still applies. Re-running the script is idempotent for the managed files listed below; all other
sysroot content is left untouched.

### Copied files

| Source (device) | Destination (sysroot) | Contents |
|---|---|---|
| `/usr/include/aarch64-linux-gnu/NvInfer*.h` | `usr/include/aarch64-linux-gnu/` | TensorRT headers |
| `/usr/include/aarch64-linux-gnu/NvOnnx*.h` | `usr/include/aarch64-linux-gnu/` | ONNX parser headers |
| `/usr/local/cuda/include/` (whole tree) | `usr/local/cuda/include/` | CUDA toolkit headers (`cuda_runtime.h`, `cuda.h`, `crt/host_config.h`, ...) |
| `/usr/lib/aarch64-linux-gnu/libnvinfer.so*` | `usr/lib/aarch64-linux-gnu/` | TensorRT dev symlinks + `libnvinfer.so.8.5.2` |
| `/usr/lib/aarch64-linux-gnu/libnvonnxparser.so*` | `usr/lib/aarch64-linux-gnu/` | ONNX parser dev symlinks + `libnvonnxparser.so.8.5.2` |
| `/usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so*` | `usr/lib/aarch64-linux-gnu/` | TensorRT plugin dev symlinks + `libnvinfer_plugin.so.8.5.2` (from `libnvinfer-plugin-dev`) |
| `/usr/local/cuda/lib64/libcudart.so*` | `usr/local/cuda/lib64/` | CUDA runtime dev symlinks + `libcudart.so.11.4.298` |
| `/usr/local/cuda/lib64/libcudla.so*` | `usr/local/cuda/lib64/` | CUDA DLA runtime dev symlinks + `libcudla.so.1.0.0` (from `libcudla-11-4`) |
| `/usr/local/cuda/lib64/libcublas.so*` | `usr/local/cuda/lib64/` | cuBLAS dev symlinks + `libcublas.so.11.6.6.84` (from `libcublas-11-4`) |
| `/usr/local/cuda/lib64/libcublasLt.so*` | `usr/local/cuda/lib64/` | cuBLAS-Lt dev symlinks + `libcublasLt.so.11.6.6.84` (from `libcublas-11-4`) |
| `/usr/lib/aarch64-linux-gnu/libcudnn.so.8*` | `usr/lib/aarch64-linux-gnu/` | cuDNN runtime `libcudnn.so.8` -> `libcudnn.so.8.6.0` (from `libcudnn8`; file name carries the 8.6.0 sub-version) |

The unversioned device `libcudnn.so` is an alternatives-managed link (`/etc/alternatives/libcudnn_so ->
/usr/lib/aarch64-linux-gnu/libcudnn.so.8`), so it is intentionally NOT copied into the sysroot. Link against
the versioned name instead: `-l:libcudnn.so.8`.

`libnvdla_compiler.so` and `libnvdla_runtime.so` were already present in the Phase 2 sysroot at
`usr/lib/aarch64-linux-gnu/tegra/`; no copy was needed (verified identical size on device and sysroot).

The library symlink chains are restored to exactly match the device layout (plain `scp` dereferences symlinks,
so the duplicated full copies were removed and re-linked):

```text
libnvinfer.so -> libnvinfer.so.8.5.2
libnvinfer.so.8 -> libnvinfer.so.8.5.2
libnvonnxparser.so -> libnvonnxparser.so.8
libnvonnxparser.so.8 -> libnvonnxparser.so.8.5.2
libnvinfer_plugin.so -> libnvinfer_plugin.so.8.5.2
libnvinfer_plugin.so.8 -> libnvinfer_plugin.so.8.5.2
libcudart.so -> libcudart.so.11.0
libcudart.so.11.0 -> libcudart.so.11.4.298
libcudla.so -> libcudla.so.1
libcudla.so.1 -> libcudla.so.1.0.0
libcublas.so -> libcublas.so.11
libcublas.so.11 -> libcublas.so.11.6.6.84
libcublasLt.so -> libcublasLt.so.11
libcublasLt.so.11 -> libcublasLt.so.11.6.6.84
libcudnn.so.8 -> libcudnn.so.8.6.0
```

A minimal inference link test (`NvInfer.h` runtime + `cuda_runtime.h` + `cublas_v2.h`, linked with
`-lnvinfer -lnvonnxparser -lnvinfer_plugin -lcudart -lcudla -lcublas -lcublasLt -l:libcudnn.so.8` and
`-L <sysroot>/usr/lib/aarch64-linux-gnu/tegra` for the DLA compiler) closes the full transitive dependency
chain with no undefined references and no `--allow-shlib-undefined`.

### Verification

```bash
bash scripts/check_inference_sysroot.sh
# Expected: PASS: inference sysroot extension present
```

The checker itself reports a single final line. When the extension is applied through
`scripts/extend_sysroot_from_device.sh`, the final output is two lines:

```text
PASS: inference sysroot extension present   (from the checker)
DONE: sysroot extension applied to <abs sysroot>
```

The sysroot under check is resolved with the priority `--sysroot <path>` > `$MANIFOLD3_SYSROOT` >
`<repo>/sysroot`:

```bash
bash scripts/check_inference_sysroot.sh --sysroot /opt/m3-sysroot
```

The checker verifies that `NvInfer.h`, `NvOnnxParser.h`, `cuda_runtime.h`, `cuda.h`, `crt/host_config.h`
are regular, non-empty files; that every managed real library (`libnvinfer.so.8.5.2`, `libnvonnxparser.so.8.5.2`,
`libnvinfer_plugin.so.8.5.2`, `libcudnn.so.8.6.0`, `libcudart.so.11.4.298`, `libcudla.so.1.0.0`,
`libcublas.so.11.6.6.84`, `libcublasLt.so.11.6.6.84`) is a regular, non-empty AArch64 ELF object with the exact
device SONAME; that every dev symlink is a symlink (`-L`) whose `readlink` target matches the device exactly;
that every link chain resolves to an existing file; and that the unversioned `libcudnn.so` does not exist
(`[ -e ] || [ -L ]`, so a dangling link is also rejected).

Note: this extension supersedes the deferred "CUDA Toolkit" and "TensorRT" rows of the Phase 2 table above for
development files. The cuDNN runtime library is present (link closure); cuDNN headers remain deferred until the
inference code calls cuDNN APIs directly.

## Development Credentials

The PSDK connects to the aircraft only with real DJI developer credentials
(App ID, App Key, App License, developer account). DJI does not provide
official sample credentials; the `164884` value in the PSDK sample `app.json`
is only a format example and has no matching key.

Credentials are injected at build time and are never committed:

```bash
cmake --preset manifold3-cross-release \
    -DMANIFOLD3_APP_ID=<id> \
    -DMANIFOLD3_APP_KEY=<key> \
    -DMANIFOLD3_APP_LICENSE=<license> \
    -DMANIFOLD3_APP_NAME=<name> \
    -DMANIFOLD3_DEVELOPER_ACCOUNT=<account>
```

The defaults are `your_app_*` placeholders. The application rejects them with
a descriptive error and exit code 1 at startup, and `app.json` is generated
from the same CMake variables so `user_app_id` always matches the compiled-in
ID (a DPK install requirement). DPK packaging, installation, start, stop, and
uninstall do not require valid credentials; only the aircraft connection does.

## Link Policy

The minimal PSDK application links:

```text
third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
-pthread (link option, not -lpthread)
libm
libdl
```

Dependency handling is divided into three groups:

| Dependency type | Policy |
|---|---|
| PSDK | Link the supplied AArch64 `libpayloadsdk.a`. |
| Ordinary third-party libraries | Prefer static libraries to comply with Manifold 3 DPK guidance. |
| CUDA, TensorRT, and driver stack | Link against the matching development files and use the compatible runtime libraries installed by the Manifold 3 firmware. |

Static `libstdc++` and `libgcc` are not defaults. They may be evaluated only if target testing identifies a real
`GLIBCXX_*` compatibility problem.

## Runtime Verification

Before a target artifact is accepted, inspect it with:

```bash
file <binary>
readelf -h <binary>
readelf -d <binary>
readelf --version-info <binary>
# Run on Manifold 3 after deployment:
ldd <binary>
```

The checks must establish that:

- the ELF architecture is AArch64;
- no host x86_64 path or library was linked;
- required `GLIBC_*` and `GLIBCXX_*` versions are available on the target;
- the CXX dependency set includes `libm.so.6`: the Phase 5 sysroot ships the device-derived
  `libstdc++.so.6`, whose own `DT_NEEDED` includes `libm.so.6`, and the linker propagates that
  inherited dependency into every C++ binary (see `tests/toolchain/verify_elf.cmake`);
- CUDA and TensorRT SONAMEs match the target firmware;
- no unintended absolute RPATH or RUNPATH is present;
- the program starts both during direct deployment and from the DPK application environment.

## Deferred Decisions

The following decisions require implementation evidence and are intentionally not fixed by the scaffold:

- whether preprocessing needs OpenCV, VPI, or custom CUDA kernels;
- whether H.264 decoding requires FFmpeg or GStreamer;
- whether TensorRT engines are generated on the host or on Manifold 3;
- whether GPU, DLA, FP16, or INT8 is selected for production;
- whether any Manifold 3 firmware libraries must overlay the standard r35.5.0 sysroot.
