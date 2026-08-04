#!/usr/bin/env bash
# Fetch the trained ONNX model + model.yaml from the HF private model repo
# into the git-ignored local models dir (.local/models/).
#
# The onboard inference path consumes the real model in Phase 5B. The runtime
# TensorRT .engine is NOT fetched here: it is built on-device from the ONNX via
# trtexec (device-specific, never committed). This script only fetches the
# source artifact and its ABI contract.
#
# Usage:
#   scripts/fetch_model.sh [--version <tag>] [--repo <hf-repo>] [--out <dir>]
#
# Defaults: repo zyzh0/tree-crown-yolo11-seg, out .local/models, version main.
# Use --version to pin a released model tag (recommended for reproducibility).
#
# Access: SSH (git@hf.co) is used; a key must be configured for HF. No token is
# read from credentials.env for SSH pulls.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_REPO="zyzh0/tree-crown-yolo11-seg"
DEFAULT_OUT="${REPO_ROOT}/.local/models"
REPO="${DEFAULT_REPO}"
VERSION="main"
OUT="${DEFAULT_OUT}"
ONNX_NAME="model.onnx"
META_NAME="model.yaml"

usage() {
    sed -n '2,16p' "${BASH_SOURCE[0]}"
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --repo)
            REPO="$2"
            shift 2
            ;;
        --out)
            OUT="$2"
            shift 2
            ;;
        --help|-h)
            usage
            ;;
        *)
            usage
            ;;
    esac
done

mkdir -p "${OUT}"

# Use a temporary clone so the ref is pinned and we can verify pointers.
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

echo "Fetching model ${REPO}@${VERSION} -> ${OUT}"
# git clone of a HF repo carries the LFS pointers; the LFS files are materialized
# because HF tracks the model files as LFS. SSH URL, no password prompt.
git clone -q --branch "${VERSION}" --depth 1 "git@hf.co:${REPO}" "${TMP_DIR}/repo"

ONNX_SRC="${TMP_DIR}/repo/${ONNX_NAME}"
META_SRC="${TMP_DIR}/repo/${META_NAME}"
if [ ! -f "${ONNX_SRC}" ]; then
    echo "ERROR: ${ONNX_NAME} not found in ${REPO}@${VERSION}" >&2
    exit 1
fi
if [ ! -f "${META_SRC}" ]; then
    echo "ERROR: ${META_NAME} not found in ${REPO}@${VERSION}" >&2
    exit 1
fi

cp "${ONNX_SRC}" "${OUT}/${ONNX_NAME}"
cp "${META_SRC}" "${OUT}/${META_NAME}"

echo "Fetched:"
echo "  ${OUT}/${ONNX_NAME}"
echo "  ${OUT}/${META_NAME}"
echo "SHA256:"
sha256sum "${OUT}/${ONNX_NAME}" "${OUT}/${META_NAME}"