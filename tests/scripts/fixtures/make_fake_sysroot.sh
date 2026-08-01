#!/usr/bin/env bash
# Builds a fake Phase 5 sysroot extension tree (host-test fixture).
#
# The tree mirrors the device layout documented in docs/build-environment.md
# ("Phase 5 Sysroot Extension"): TensorRT/CUDA headers, real library files
# with the exact device SONAMEs, and the exact device symlink chains. It is
# used both as the sysroot under test by test_check_inference_sysroot.sh and
# as the fake device tree that fake_scp copies from in
# test_extend_sysroot_from_device.sh.
set -euo pipefail

FIXTURES="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${1:?usage: make_fake_sysroot.sh <dest>}"

HDR="${DEST}/usr/include/aarch64-linux-gnu"
CUDA_HDR="${DEST}/usr/local/cuda/include"
LIB="${DEST}/usr/lib/aarch64-linux-gnu"
CUDA_LIB="${DEST}/usr/local/cuda/lib64"

mkdir -p "${HDR}" "${CUDA_HDR}/crt" "${LIB}" "${CUDA_LIB}"

# Headers: regular, non-empty files.
for h in NvInfer.h NvInferRuntime.h NvOnnxParser.h; do
    printf '/* fake %s */\n' "${h}" >"${HDR}/${h}"
done
for h in cuda_runtime.h cuda.h; do
    printf '/* fake %s */\n' "${h}" >"${CUDA_HDR}/${h}"
done
printf '/* fake host_config.h */\n' >"${CUDA_HDR}/crt/host_config.h"

# Real library files: minimal AArch64 ELF objects with the device SONAME.
make_so() { python3 "${FIXTURES}/make_fake_elf.py" "$1" "$2"; }

make_so "${LIB}/libnvinfer.so.8.5.2" libnvinfer.so.8
ln -s libnvinfer.so.8.5.2 "${LIB}/libnvinfer.so.8"
ln -s libnvinfer.so.8.5.2 "${LIB}/libnvinfer.so"

make_so "${LIB}/libnvonnxparser.so.8.5.2" libnvonnxparser.so.8
ln -s libnvonnxparser.so.8.5.2 "${LIB}/libnvonnxparser.so.8"
ln -s libnvonnxparser.so.8 "${LIB}/libnvonnxparser.so"

make_so "${LIB}/libnvinfer_plugin.so.8.5.2" libnvinfer_plugin.so.8
ln -s libnvinfer_plugin.so.8.5.2 "${LIB}/libnvinfer_plugin.so.8"
ln -s libnvinfer_plugin.so.8.5.2 "${LIB}/libnvinfer_plugin.so"

make_so "${LIB}/libcudnn.so.8.6.0" libcudnn.so.8
ln -s libcudnn.so.8.6.0 "${LIB}/libcudnn.so.8"

make_so "${CUDA_LIB}/libcudart.so.11.4.298" libcudart.so.11.0
ln -s libcudart.so.11.4.298 "${CUDA_LIB}/libcudart.so.11.0"
ln -s libcudart.so.11.0 "${CUDA_LIB}/libcudart.so"

make_so "${CUDA_LIB}/libcudla.so.1.0.0" libcudla.so.1
ln -s libcudla.so.1.0.0 "${CUDA_LIB}/libcudla.so.1"
ln -s libcudla.so.1 "${CUDA_LIB}/libcudla.so"

make_so "${CUDA_LIB}/libcublas.so.11.6.6.84" libcublas.so.11
ln -s libcublas.so.11.6.6.84 "${CUDA_LIB}/libcublas.so.11"
ln -s libcublas.so.11 "${CUDA_LIB}/libcublas.so"

make_so "${CUDA_LIB}/libcublasLt.so.11.6.6.84" libcublasLt.so.11
ln -s libcublasLt.so.11.6.6.84 "${CUDA_LIB}/libcublasLt.so.11"
ln -s libcublasLt.so.11 "${CUDA_LIB}/libcublasLt.so"
