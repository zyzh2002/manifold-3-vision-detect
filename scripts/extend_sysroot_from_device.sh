#!/usr/bin/env bash
# Extend the cross sysroot with CUDA/TensorRT development files from a
# connected Manifold 3 device.
#
# The Manifold 3 firmware is the runtime authority, so the Phase 5 sysroot
# extension is copied from the device (see docs/build-environment.md,
# "Phase 5 Sysroot Extension"). Plain scp dereferences remote symlinks into
# full file copies, so this script restores the device symlink layout after
# copying. It is idempotent: re-running against an already-extended sysroot
# replaces the files and re-links the symlinks to the same result.
#
# Usage:
#   scripts/extend_sysroot_from_device.sh [manifold3-ip] [--sysroot <path>]
#
# Examples:
#   scripts/extend_sysroot_from_device.sh                # 192.168.42.120, $MANIFOLD3_SYSROOT or ./sysroot
#   scripts/extend_sysroot_from_device.sh 10.0.0.5
#   scripts/extend_sysroot_from_device.sh --sysroot /opt/m3-sysroot

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)

TARGET_IP="192.168.42.120"
SYSROOT="${MANIFOLD3_SYSROOT:-${REPO_ROOT}/sysroot}"

while [ $# -gt 0 ]; do
    case "$1" in
        --sysroot)
            SYSROOT="$2"
            shift 2
            ;;
        -*)
            echo "usage: $0 [manifold3-ip] [--sysroot <path>]" >&2
            exit 2
            ;;
        *)
            TARGET_IP="$1"
            shift
            ;;
    esac
done

# Device package versions this extension is built against (documented in
# docs/build-environment.md "Device packages (recorded 2026-07-31)").
EXPECTED_PACKAGES=(
    "libnvinfer-dev 8.5.2-1+cuda11.4"
    "libnvonnxparsers8 8.5.2-1+cuda11.4"
    "libcudart 11.4.298-1"
    "libcudla 11.4.298-1"
    "libcublas 11.6.6.84-1"
    "libcudnn8 8.6.0.166-1+cuda11.4"
)

# Symlink layouts to restore after scp (matches the device exactly). Keys are
# the sysroot-relative real file paths; values are "link:target" pairs to
# create with ln -s, listed in device order (the last one is the unversioned
# dev link used by -l<name>).
declare -A SYMLINKS=(
    ["usr/lib/aarch64-linux-gnu/libnvinfer.so.8.5.2"]="libnvinfer.so.8:libnvinfer.so.8.5.2 libnvinfer.so:libnvinfer.so.8.5.2"
    ["usr/lib/aarch64-linux-gnu/libnvonnxparser.so.8.5.2"]="libnvonnxparser.so.8:libnvonnxparser.so.8.5.2 libnvonnxparser.so:libnvonnxparser.so.8"
    ["usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so.8.5.2"]="libnvinfer_plugin.so.8:libnvinfer_plugin.so.8.5.2 libnvinfer_plugin.so:libnvinfer_plugin.so.8.5.2"
    ["usr/lib/aarch64-linux-gnu/libcudnn.so.8.6.0"]="libcudnn.so.8:libcudnn.so.8.6.0"
    ["usr/local/cuda/lib64/libcudart.so.11.4.298"]="libcudart.so.11.0:libcudart.so.11.4.298 libcudart.so:libcudart.so.11.0"
    ["usr/local/cuda/lib64/libcudla.so.1.0.0"]="libcudla.so.1:libcudla.so.1.0.0 libcudla.so:libcudla.so.1"
    ["usr/local/cuda/lib64/libcublas.so.11.6.6.84"]="libcublas.so.11:libcublas.so.11.6.6.84 libcublas.so:libcublas.so.11"
    ["usr/local/cuda/lib64/libcublasLt.so.11.6.6.84"]="libcublasLt.so.11:libcublasLt.so.11.6.6.84 libcublasLt.so:libcublasLt.so.11"
)

ssh_cmd() {
    ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "$@"
}

scp_cmd() {
    scp "${SSH_OPTS[@]}" "$@"
}

# --- Precondition checks ---
echo "Checking device ${TARGET_IP} ..."
if ! ssh_cmd "echo reachable" >/dev/null 2>&1; then
    echo "ERROR: device ${TARGET_IP} not reachable (key: ${SSH_KEY})" >&2
    echo "Connect the Manifold 3 via USB and confirm the network is up." >&2
    exit 1
fi

echo "Verifying device package versions ..."
version_mismatch=0
for spec in "${EXPECTED_PACKAGES[@]}"; do
    pkg="${spec%% *}"
    ver="${spec#* }"
    # Match a package whose name starts with the expected name (covers
    # cuda-cudart-11-4 vs cuda-cudart-dev-11-4 and -dev variants). Each
    # matching package prints one version line; empty-version rows (e.g.
    # libcudla.so.1 alternatives rows) are dropped and the rest must agree.
    actual=$(ssh_cmd "dpkg-query -W -f='\${Version}\n' '${pkg}*' 2>/dev/null" | tr -d "'" | grep -v '^$' | sort -u | head -1)
    if [ -n "${actual}" ] && [ "${actual}" != "${ver}" ]; then
        echo "WARNING: ${pkg} version ${actual} != expected ${ver}" >&2
        version_mismatch=1
    fi
done
if [ "${version_mismatch}" -eq 1 ]; then
    echo "WARNING: device package versions differ from the documented baseline;" >&2
    echo "  the copied files may not match docs/build-environment.md." >&2
fi

# --- Copy TensorRT headers ---
echo "Copying TensorRT headers ..."
mkdir -p "${SYSROOT}/usr/include/aarch64-linux-gnu"
scp_cmd "dji@${TARGET_IP}:/usr/include/aarch64-linux-gnu/NvInfer*.h" \
    "dji@${TARGET_IP}:/usr/include/aarch64-linux-gnu/NvOnnx*.h" \
    "${SYSROOT}/usr/include/aarch64-linux-gnu/"

# --- Copy CUDA toolkit headers (whole tree; cuda_runtime.h pulls cuda.h,
# --- crt/host_config.h and friends) ---
echo "Copying CUDA toolkit headers ..."
mkdir -p "${SYSROOT}/usr/local/cuda/include"
scp_cmd -r "dji@${TARGET_IP}:/usr/local/cuda/include/." "${SYSROOT}/usr/local/cuda/include/"

# --- Copy libraries ---
echo "Copying TensorRT libraries ..."
mkdir -p "${SYSROOT}/usr/lib/aarch64-linux-gnu"
scp_cmd "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libnvinfer.so*" \
    "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libnvonnxparser.so*" \
    "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so*" \
    "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libcudnn.so.8*" \
    "${SYSROOT}/usr/lib/aarch64-linux-gnu/"

echo "Copying CUDA runtime libraries ..."
mkdir -p "${SYSROOT}/usr/local/cuda/lib64"
scp_cmd "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcudart.so*" \
    "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcudla.so*" \
    "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcublas.so*" \
    "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcublasLt.so*" \
    "${SYSROOT}/usr/local/cuda/lib64/"

# --- Restore the device symlink layout ---
# scp dereferences symlinks into full copies; remove the duplicated dev links
# and re-link them exactly as on the device.
echo "Restoring symlink layout ..."
for real_file in "${!SYMLINKS[@]}"; do
    dir="${SYSROOT}/$(dirname "${real_file}")"
    real_name="$(basename "${real_file}")"
    if [ ! -f "${SYSROOT}/${real_file}" ]; then
        echo "WARNING: expected real file missing: ${real_file}" >&2
        continue
    fi
    for pair in ${SYMLINKS[${real_file}]}; do
        link="${pair%%:*}"
        target="${pair#*:}"
        rm -f "${dir}/${link}"
        ln -s "${target}" "${dir}/${link}"
    done
done

# The unversioned device libcudnn.so is an alternatives-managed link and is
# intentionally not copied; link against the versioned name (-l:libcudnn.so.8).
if [ -e "${SYSROOT}/usr/lib/aarch64-linux-gnu/libcudnn.so" ]; then
    rm -f "${SYSROOT}/usr/lib/aarch64-linux-gnu/libcudnn.so"
fi

# --- Verify ---
echo "Verifying sysroot extension ..."
bash "${REPO_ROOT}/scripts/check_inference_sysroot.sh"
echo "DONE: sysroot extension applied to ${SYSROOT}"
