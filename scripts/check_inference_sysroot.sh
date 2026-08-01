#!/usr/bin/env bash
# Verifies the Phase 5 sysroot extension for TensorRT inference builds.
#
# Checks the managed extension files (see docs/build-environment.md "Phase 5
# Sysroot Extension"): headers are regular non-empty files; library real
# files are regular non-empty AArch64 ELF objects with the exact device
# SONAME; dev symlinks are symlinks pointing at the exact device targets;
# every link chain resolves; and the unversioned libcudnn.so is absent.
#
# Usage:
#   scripts/check_inference_sysroot.sh [--sysroot <path>]
#
# Sysroot resolution priority: --sysroot > $MANIFOLD3_SYSROOT > repo/sysroot.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSROOT="${MANIFOLD3_SYSROOT:-${REPO_ROOT}/sysroot}"

usage() {
    echo "usage: $0 [--sysroot <path>]" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --sysroot)
            if [ $# -lt 2 ]; then
                usage
            fi
            SYSROOT="$2"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

HDR="${SYSROOT}/usr/include/aarch64-linux-gnu"
LIB="${SYSROOT}/usr/lib/aarch64-linux-gnu"
CUDA_HDR="${SYSROOT}/usr/local/cuda/include"
CUDA_LIB="${SYSROOT}/usr/local/cuda/lib64"

# Headers must be regular, non-empty files.
for f in "${HDR}/NvInfer.h" "${HDR}/NvOnnxParser.h" \
         "${CUDA_HDR}/cuda_runtime.h" "${CUDA_HDR}/cuda.h" \
         "${CUDA_HDR}/crt/host_config.h"; do
    if [ ! -f "$f" ]; then
        fail "missing or not a regular file: $f"
    fi
    if [ ! -s "$f" ]; then
        fail "empty file: $f"
    fi
done

# Real library files must be regular, non-empty AArch64 ELF objects with the
# exact device SONAME.
REAL_LIBS=(
    "${LIB}/libnvinfer.so.8.5.2:libnvinfer.so.8"
    "${LIB}/libnvonnxparser.so.8.5.2:libnvonnxparser.so.8"
    "${LIB}/libnvinfer_plugin.so.8.5.2:libnvinfer_plugin.so.8"
    "${LIB}/libcudnn.so.8.6.0:libcudnn.so.8"
    "${CUDA_LIB}/libcudart.so.11.4.298:libcudart.so.11.0"
    "${CUDA_LIB}/libcudla.so.1.0.0:libcudla.so.1"
    "${CUDA_LIB}/libcublas.so.11.6.6.84:libcublas.so.11"
    "${CUDA_LIB}/libcublasLt.so.11.6.6.84:libcublasLt.so.11"
)
for spec in "${REAL_LIBS[@]}"; do
    f="${spec%%:*}"
    soname="${spec#*:}"
    if [ ! -f "$f" ]; then
        fail "missing or not a regular file: $f"
    fi
    if [ ! -s "$f" ]; then
        fail "empty file: $f"
    fi
    if ! machine="$(readelf -h "$f" 2>/dev/null | awk '/Machine:/{print $2}')"; then
        fail "unreadable ELF header: $f"
    fi
    if [ "${machine}" != "AArch64" ]; then
        fail "not an AArch64 ELF: $f (machine: ${machine})"
    fi
    if ! actual="$(readelf -d "$f" 2>/dev/null |
        sed -n 's/.*(SONAME)[[:space:]]*Library soname: \[\(.*\)\]/\1/p')"; then
        fail "unreadable dynamic section: $f"
    fi
    if [ "${actual}" != "${soname}" ]; then
        fail "unexpected SONAME for $f: got '${actual:-none}', want '${soname}'"
    fi
done

# Dev symlinks must be symlinks pointing at the exact device targets.
LINKS=(
    "${LIB}/libnvinfer.so:libnvinfer.so.8.5.2"
    "${LIB}/libnvinfer.so.8:libnvinfer.so.8.5.2"
    "${LIB}/libnvonnxparser.so:libnvonnxparser.so.8"
    "${LIB}/libnvonnxparser.so.8:libnvonnxparser.so.8.5.2"
    "${LIB}/libnvinfer_plugin.so:libnvinfer_plugin.so.8.5.2"
    "${LIB}/libnvinfer_plugin.so.8:libnvinfer_plugin.so.8.5.2"
    "${LIB}/libcudnn.so.8:libcudnn.so.8.6.0"
    "${CUDA_LIB}/libcudart.so:libcudart.so.11.0"
    "${CUDA_LIB}/libcudart.so.11.0:libcudart.so.11.4.298"
    "${CUDA_LIB}/libcudla.so:libcudla.so.1"
    "${CUDA_LIB}/libcudla.so.1:libcudla.so.1.0.0"
    "${CUDA_LIB}/libcublas.so:libcublas.so.11"
    "${CUDA_LIB}/libcublas.so.11:libcublas.so.11.6.6.84"
    "${CUDA_LIB}/libcublasLt.so:libcublasLt.so.11"
    "${CUDA_LIB}/libcublasLt.so.11:libcublasLt.so.11.6.6.84"
)
for spec in "${LINKS[@]}"; do
    f="${spec%%:*}"
    target="${spec#*:}"
    if [ ! -L "$f" ]; then
        fail "not a symlink: $f"
    fi
    actual="$(readlink "$f")"
    if [ "${actual}" != "${target}" ]; then
        fail "unexpected readlink for $f: got '${actual}', want '${target}'"
    fi
done

# The unversioned device libcudnn.so is alternatives-managed and must NOT be
# copied into the sysroot. `-e` alone misses a dangling link, so test -L too.
if [ -e "${LIB}/libcudnn.so" ] || [ -L "${LIB}/libcudnn.so" ]; then
    fail "libcudnn.so must not exist (link against -l:libcudnn.so.8)"
fi

# Full link closure: every chain resolves to an existing regular file.
for spec in "${LINKS[@]}"; do
    f="${spec%%:*}"
    if [ ! -f "$(readlink -f "$f")" ]; then
        fail "dangling link chain: $f"
    fi
done

echo "PASS: inference sysroot extension present"
