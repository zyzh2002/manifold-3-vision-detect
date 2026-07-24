# Target Toolchain

The primary target compiler is NVIDIA's prebuilt Bootlin GCC 9.3.0 AArch64 toolchain for Jetson Linux r35.5.0.

The compiler and sysroot are local development dependencies and are not stored in Git:

```bash
export MANIFOLD3_TOOLCHAIN_DIR=/opt/toolchains/bootlin-gcc-9.3
export MANIFOLD3_SYSROOT="$(git rev-parse --show-toplevel)/sysroot"
```

The local `sysroot/` and optional `.local-toolchains/` directories are ignored. See
[`docs/build-environment.md`](../docs/build-environment.md) for the complete baseline, sysroot composition, link policy,
and verification requirements.

crosstool-ng is a fallback, not part of the default setup. Add a crosstool-ng configuration only after documenting a
specific limitation that cannot be resolved with NVIDIA's supported toolchain.
