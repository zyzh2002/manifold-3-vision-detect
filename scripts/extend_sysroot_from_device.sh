#!/usr/bin/env bash
# Extend the cross sysroot with CUDA/TensorRT development files from a
# connected Manifold 3 device.
#
# The Manifold 3 firmware is the runtime authority, so the Phase 5 sysroot
# extension is copied from the device (see docs/build-environment.md,
# "Phase 5 Sysroot Extension"). The script verifies the exact installed
# versions of every package that provides the copied files and hard-fails
# before any copy when a package is missing or its version differs from the
# documented baseline.
#
# The copy first goes into a staging directory created with mktemp -d inside
# the sysroot (same filesystem), and a cleanup trap removes it on EXIT/INT/
# TERM. The staged tree is then verified with check_inference_sysroot.sh
# before anything is installed, so a failed or interrupted run never leaves a
# partial extension in the sysroot. Plain scp dereferences remote symlinks
# into full file copies, so the script restores the device symlink layout in
# staging before installing. The final install replaces the previously
# managed files only and is idempotent for them: re-running against an
# already-extended sysroot yields the same managed file tree.
#
# Usage:
#   scripts/extend_sysroot_from_device.sh [manifold3-ip] [--sysroot <path>] [--no-verify]
#
# Examples:
#   scripts/extend_sysroot_from_device.sh                # 192.168.42.120, $MANIFOLD3_SYSROOT or ./sysroot
#   scripts/extend_sysroot_from_device.sh 10.0.0.5
#   scripts/extend_sysroot_from_device.sh --sysroot /opt/m3-sysroot
#   scripts/extend_sysroot_from_device.sh --no-verify    # skip checker runs (host tests only)
#
# Final output:
#   PASS: inference sysroot extension present   (from the checker)
#   DONE: sysroot extension applied to <abs sysroot>

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)
STAGING=""

usage() {
    echo "usage: $0 [manifold3-ip] [--sysroot <path>] [--no-verify]" >&2
    echo "  --sysroot <path>  sysroot to extend (default: \$MANIFOLD3_SYSROOT or ./sysroot)" >&2
    echo "  --no-verify       skip the check_inference_sysroot.sh runs (tests only)" >&2
    exit 2
}

cleanup() {
    if [ -n "${STAGING}" ]; then
        rm -rf -- "${STAGING}"
    fi
}

TARGET_IP="192.168.42.120"
SYSROOT="${MANIFOLD3_SYSROOT:-${REPO_ROOT}/sysroot}"
NO_VERIFY=0
positional_count=0

while [ $# -gt 0 ]; do
    case "$1" in
        --sysroot)
            if [ $# -lt 2 ]; then
                usage
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --no-verify)
            NO_VERIFY=1
            shift
            ;;
        -*)
            usage
            ;;
        *)
            positional_count=$((positional_count + 1))
            if [ "${positional_count}" -gt 1 ]; then
                usage
            fi
            TARGET_IP="$1"
            shift
            ;;
    esac
done

# The script copies INTO the sysroot, so create it when missing and
# normalize it to an absolute path for unambiguous messages and downstream
# use.
mkdir -p "${SYSROOT}"
SYSROOT="$(realpath "${SYSROOT}")"

# Cleanup on every exit path: a normal exit, an exit from the INT/TERM
# traps, or an early failure removes the staging directory. INT/TERM first
# exit with the conventional 128+signal status so the EXIT trap runs.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [ ! -f "${SSH_KEY}" ]; then
    echo "ERROR: SSH key not found: ${SSH_KEY}" >&2
    echo "  Restore config/manifold3_id_rsa (see AGENTS.md)." >&2
    exit 1
fi

# Exact device package versions this extension is built against (documented
# in docs/build-environment.md "Device packages (recorded 2026-07-31)").
# Every package is queried by exact name and the installed version must
# match exactly; a missing package or a version difference aborts before
# any file is copied.
EXPECTED_PACKAGES=(
    "cuda-cudart-11-4 11.4.298-1"
    "cuda-cudart-dev-11-4 11.4.298-1"
    "libcudla-11-4 11.4.298-1"
    "libcudla-dev-11-4 11.4.298-1"
    "libcublas-11-4 11.6.6.84-1"
    "libcublas-dev-11-4 11.6.6.84-1"
    "libcudnn8 8.6.0.166-1+cuda11.4"
    "libcudnn8-dev 8.6.0.166-1+cuda11.4"
    "libnvinfer8 8.5.2-1+cuda11.4"
    "libnvinfer-dev 8.5.2-1+cuda11.4"
    "libnvinfer-plugin8 8.5.2-1+cuda11.4"
    "libnvinfer-plugin-dev 8.5.2-1+cuda11.4"
    "libnvonnxparsers8 8.5.2-1+cuda11.4"
    "libnvonnxparsers-dev 8.5.2-1+cuda11.4"
)

# Symlink layouts to restore in staging after scp (matches the device
# exactly). Keys are the sysroot-relative real file paths; values are
# "link:target" pairs to create with ln -s, listed in device order (the
# last one is the unversioned dev link used by -l<name>).
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
for spec in "${EXPECTED_PACKAGES[@]}"; do
    pkg="${spec%% *}"
    ver="${spec#* }"
    # Query the exact package name; dpkg-query exits non-zero when the
    # package is not installed. The `if` guards the failure explicitly so
    # `set -e` cannot abort with an unhelpful trace.
    if actual=$(ssh_cmd "dpkg-query -W -f='\${Version}' '${pkg}'"); then
        if [ "${actual}" != "${ver}" ]; then
            echo "ERROR: ${pkg} version ${actual} != expected ${ver}" >&2
            exit 1
        fi
    else
        echo "ERROR: package ${pkg} missing on device ${TARGET_IP}" >&2
        exit 1
    fi
    echo "OK: ${pkg} ${actual}"
done

# --- Stage the copy ---
# The staging directory lives inside the sysroot so the final install is a
# same-filesystem rename; the cleanup trap removes it on any exit path.
STAGING="$(mktemp -d "${SYSROOT}/.sysroot-staging.XXXXXX")"
echo "Staging copy in ${STAGING} ..."

mkdir -p "${STAGING}/usr/include/aarch64-linux-gnu"
mkdir -p "${STAGING}/usr/local/cuda/include"
mkdir -p "${STAGING}/usr/lib/aarch64-linux-gnu"
mkdir -p "${STAGING}/usr/local/cuda/lib64"

# --- Copy TensorRT headers ---
echo "Copying TensorRT headers ..."
scp_cmd "dji@${TARGET_IP}:/usr/include/aarch64-linux-gnu/NvInfer*.h" \
    "dji@${TARGET_IP}:/usr/include/aarch64-linux-gnu/NvOnnx*.h" \
    "${STAGING}/usr/include/aarch64-linux-gnu/"

# --- Copy CUDA toolkit headers (whole tree; cuda_runtime.h pulls cuda.h,
# --- crt/host_config.h and friends) ---
echo "Copying CUDA toolkit headers ..."
scp_cmd -r "dji@${TARGET_IP}:/usr/local/cuda/include/." "${STAGING}/usr/local/cuda/include/"

# --- Copy libraries ---
echo "Copying TensorRT libraries ..."
scp_cmd "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libnvinfer.so*" \
    "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libnvonnxparser.so*" \
    "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libnvinfer_plugin.so*" \
    "dji@${TARGET_IP}:/usr/lib/aarch64-linux-gnu/libcudnn.so.8*" \
    "${STAGING}/usr/lib/aarch64-linux-gnu/"

echo "Copying CUDA runtime libraries ..."
scp_cmd "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcudart.so*" \
    "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcudla.so*" \
    "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcublas.so*" \
    "dji@${TARGET_IP}:/usr/local/cuda/lib64/libcublasLt.so*" \
    "${STAGING}/usr/local/cuda/lib64/"

# --- Restore the device symlink layout in staging ---
# scp dereferences symlinks into full copies; remove the duplicated dev links
# and re-link them exactly as on the device.
echo "Restoring symlink layout ..."
for real_file in "${!SYMLINKS[@]}"; do
    dir="${STAGING}/$(dirname "${real_file}")"
    real_name="$(basename "${real_file}")"
    if [ ! -f "${STAGING}/${real_file}" ]; then
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

# --- Verify the staged tree before anything touches the sysroot ---
if [ "${NO_VERIFY}" -eq 0 ]; then
    echo "Verifying staged extension ..."
    bash "${REPO_ROOT}/scripts/check_inference_sysroot.sh" --sysroot "${STAGING}"
fi

# --- Install from staging (same filesystem: per-file atomic renames) ---
echo "Installing sysroot extension ..."
HDR_DIR="${SYSROOT}/usr/include/aarch64-linux-gnu"
LIB_DIR="${SYSROOT}/usr/lib/aarch64-linux-gnu"
mkdir -p "${HDR_DIR}" "${SYSROOT}/usr/local" "${LIB_DIR}"

# Headers: replace only the managed TensorRT/ONNX headers.
rm -f "${HDR_DIR}"/NvInfer*.h "${HDR_DIR}"/NvOnnx*.h
mv "${STAGING}/usr/include/aarch64-linux-gnu"/NvInfer*.h \
    "${STAGING}/usr/include/aarch64-linux-gnu"/NvOnnx*.h \
    "${HDR_DIR}/"

# CUDA toolkit tree: entirely managed by the extension (the Phase 2 base
# sysroot has no /usr/local/cuda), so it is replaced as one atomic rename.
rm -rf "${SYSROOT}/usr/local/cuda"
mv "${STAGING}/usr/local/cuda" "${SYSROOT}/usr/local/cuda"

# Libraries: replace only the managed .so families.
rm -f "${LIB_DIR}"/libnvinfer.so* "${LIB_DIR}"/libnvonnxparser.so* \
    "${LIB_DIR}"/libnvinfer_plugin.so* "${LIB_DIR}"/libcudnn.so.8*
mv "${STAGING}/usr/lib/aarch64-linux-gnu"/libnvinfer.so* \
    "${STAGING}/usr/lib/aarch64-linux-gnu"/libnvonnxparser.so* \
    "${STAGING}/usr/lib/aarch64-linux-gnu"/libnvinfer_plugin.so* \
    "${STAGING}/usr/lib/aarch64-linux-gnu"/libcudnn.so.8* \
    "${LIB_DIR}/"

# The unversioned device libcudnn.so is an alternatives-managed link and is
# intentionally not copied; link against the versioned name (-l:libcudnn.so.8).
# `-e` alone misses a dangling link, so test -L too.
if [ -e "${LIB_DIR}/libcudnn.so" ] || [ -L "${LIB_DIR}/libcudnn.so" ]; then
    rm -f "${LIB_DIR}/libcudnn.so"
fi

# --- Verify the target sysroot ---
if [ "${NO_VERIFY}" -eq 0 ]; then
    echo "Verifying sysroot extension ..."
    bash "${REPO_ROOT}/scripts/check_inference_sysroot.sh" --sysroot "${SYSROOT}"
fi
echo "DONE: sysroot extension applied to ${SYSROOT}"
