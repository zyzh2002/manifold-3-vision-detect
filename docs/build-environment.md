# Build Environment

## Confirmed Baseline

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

## Toolchain Policy

- Download and use NVIDIA's prebuilt Bootlin GCC 9.3.0 AArch64 toolchain.
- Keep the toolchain outside version control. It may be installed globally or under the ignored `.local-toolchains/`
  directory.
- Use crosstool-ng only after a concrete limitation of the NVIDIA toolchain has been demonstrated and documented.
- The initial inference implementation will call TensorRT and CUDA C/C++ APIs without compiling custom `.cu` files.

The authoritative toolchain path is supplied through:

```bash
export MANIFOLD3_TOOLCHAIN_DIR=/opt/toolchains/bootlin-gcc-9.3
```

## Complete Sysroot

The target sysroot is built from the matching NVIDIA release rather than copied from the Bootlin toolchain alone:

```text
Jetson Linux r35.5.0 Driver Package (BSP)
+ Tegra Linux Sample Root Filesystem
+ JetPack 5.1.3 AArch64 development packages
```

The development packages must provide the headers, unversioned linker names, shared libraries, and transitive
dependencies needed by the application. The initial required groups are:

- glibc and standard Linux development files;
- CUDA 11.4 development files for AArch64;
- TensorRT 8.5.2 development files for AArch64;
- any transitive libraries actually required by the selected TensorRT APIs.

OpenCV, FFmpeg, GStreamer, ONNX parser, and TensorRT plugin development packages are added only when the implementation
uses them.

The complete sysroot may be stored at `sysroot/` in the repository working directory. That directory is ignored by Git
and must never be committed. A centrally managed location is also supported. The authoritative path is supplied through:

```bash
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

Do not mix files from other JetPack or Jetson Linux releases into this sysroot. CMake must use `MANIFOLD3_SYSROOT` as
the target search root and must not resolve target headers or libraries from host x86_64 directories.

The standard r35.5.0 sysroot is the build baseline. The actual Manifold 3 firmware remains the runtime authority. Copy a
file from the device only after a measured ABI or dependency difference requires a documented overlay.

## Link Policy

The minimal PSDK application links:

```text
third_party/psdk/psdk_lib/lib/aarch64-linux-gnu-gcc/libpayloadsdk.a
Threads::Threads
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
