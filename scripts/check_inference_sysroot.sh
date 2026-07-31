#!/usr/bin/env bash
# Verifies the Phase 5 sysroot extension for TensorRT inference builds.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSROOT="${MANIFOLD3_SYSROOT:-${REPO_ROOT}/sysroot}"
HDR="${SYSROOT}/usr/include/aarch64-linux-gnu"
LIB="${SYSROOT}/usr/lib/aarch64-linux-gnu"
CUDA_HDR="${SYSROOT}/usr/local/cuda/include"
CUDA_LIB="${SYSROOT}/usr/local/cuda/lib64"
missing=0
for f in "${HDR}/NvInfer.h" "${HDR}/NvOnnxParser.h" "${CUDA_HDR}/cuda_runtime.h" \
         "${CUDA_HDR}/cuda.h" "${CUDA_HDR}/crt/host_config.h"; do
    if [ ! -f "$f" ]; then echo "MISSING $f"; missing=1; fi
done
for f in "${LIB}/libnvinfer.so" "${LIB}/libnvonnxparser.so" "${CUDA_LIB}/libcudart.so"; do
    if [ ! -e "$f" ]; then echo "MISSING $f"; missing=1; fi
done
if [ "$missing" -eq 1 ]; then echo "FAIL"; exit 1; fi
echo "PASS: inference sysroot extension present"
