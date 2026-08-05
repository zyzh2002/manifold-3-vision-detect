#!/usr/bin/env bash
# Fetch a versioned model release from the HF private model repo into the
# git-ignored local models tree.
#
# Each HF tag is an immutable release containing model.onnx, model.yaml, and
# SHA256SUMS. This script resolves the exact tag commit, materializes the ONNX
# through Git LFS, verifies the publisher checksums, and atomically installs
# the release under .local/models/releases/<tag>/ with .local/models/current
# pointing at it. A failed or interrupted fetch never replaces a known-good
# release.
#
# Usage:
#   scripts/fetch_model.sh --version <tag> [--repo <owner/name>]
#
# Options:
#   --version <tag>   Immutable HF release tag (required; e.g. v1.0.0).
#   --repo <org/repo> HF model repo (default: zyzh0/tree-crown-yolo11-seg).
#   --help, -h        Show this help.
#
# Environment:
#   MANIFOLD3_MODEL_ROOT  Override the model root (default: .local/models).
#                         Used by tests to isolate from the real model tree.
#
# Access: SSH (git@hf.co) is used; an SSH key must be configured for HF.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_REPO="zyzh0/tree-crown-yolo11-seg"
DEFAULT_MODEL_ROOT="${REPO_ROOT}/.local/models"
MODEL_ROOT="${MANIFOLD3_MODEL_ROOT:-${DEFAULT_MODEL_ROOT}}"
REPO="${DEFAULT_REPO}"
VERSION=""
ONNX_NAME="model.onnx"
META_NAME="model.yaml"
MANIFEST_NAME="SHA256SUMS"

usage() {
    sed -n '2,18p' "${BASH_SOURCE[0]}"
}

require_value() {
    if [ $# -lt 2 ] || [ -z "$2" ]; then
        echo "ERROR: $1 requires a value" >&2
        usage >&2
        exit 2
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --version)
            require_value "$@"
            VERSION="$2"
            shift 2
            ;;
        --repo)
            require_value "$@"
            REPO="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

if [ -z "${VERSION}" ]; then
    echo "ERROR: --version is required" >&2
    exit 2
fi
case "${VERSION}" in
    main|master|HEAD|*/*|*..*)
        echo "ERROR: --version must be an immutable release tag" >&2
        exit 2
        ;;
esac

# --- Remote validation happens entirely in /tmp; the model root is untouched
# until the release is fully verified. ---
TMP_DIR="$(mktemp -d)"
cleanup() {
    rm -rf "${TMP_DIR:-}" "${STAGING_DIR:-}"
    [ -z "${CURRENT_TMP:-}" ] || rm -f "${CURRENT_TMP}"
    if [ "${RELEASE_CREATED:-0}" -eq 1 ] && [ "${CURRENT_COMMITTED:-0}" -eq 0 ]; then
        rm -rf "${RELEASE_DIR}"
    fi
}
on_signal() {
    status="$1"
    trap - INT TERM
    cleanup
    exit "${status}"
}
trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

HF_URL="git@hf.co:${REPO}"

# --- Resolve the exact tag commit; distinguish transport errors from a missing tag. ---
TAG_STDERR="${TMP_DIR}/ls-remote.stderr"
set +e
tag_output="$(git ls-remote --exit-code --tags "${HF_URL}" \
    "refs/tags/${VERSION}" "refs/tags/${VERSION}^{}" 2>"${TAG_STDERR}")"
tag_rc=$?
set -e

if [ "${tag_rc}" -eq 2 ]; then
    echo "ERROR: tag not found: ${REPO}@${VERSION}" >&2
    exit 1
fi
if [ "${tag_rc}" -ne 0 ]; then
    echo "ERROR: unable to access HF repo ${REPO}: $(tr '\n' ' ' <"${TAG_STDERR}")" >&2
    exit 1
fi

EXPECTED_COMMIT="$(printf '%s\n' "${tag_output}" | awk '
    $2 ~ /\^\{\}$/ { peeled=$1 }
    $2 !~ /\^\{\}$/ { direct=$1 }
    END { print peeled != "" ? peeled : direct }
')"
[ -n "${EXPECTED_COMMIT}" ] || {
    echo "ERROR: tag not found: ${REPO}@${VERSION}" >&2
    exit 1
}

# --- Require Git LFS and materialize the ONNX. ---
if ! git lfs version >/dev/null 2>&1; then
    echo "ERROR: git lfs is required to fetch model.onnx" >&2
    exit 1
fi

GIT_LFS_SKIP_SMUDGE=1 git clone -q --branch "${VERSION}" --depth 1 "${HF_URL}" "${TMP_DIR}/repo"
ACTUAL_COMMIT="$(git -C "${TMP_DIR}/repo" rev-parse HEAD)"
if [ "${ACTUAL_COMMIT}" != "${EXPECTED_COMMIT}" ]; then
    echo "ERROR: tag moved during fetch: expected ${EXPECTED_COMMIT}, got ${ACTUAL_COMMIT}" >&2
    exit 1
fi

if ! git -C "${TMP_DIR}/repo" lfs pull --include="model.onnx" --exclude=""; then
    echo "ERROR: git lfs pull failed for model.onnx" >&2
    exit 1
fi

ONNX_SRC="${TMP_DIR}/repo/${ONNX_NAME}"
META_SRC="${TMP_DIR}/repo/${META_NAME}"
MANIFEST_SRC="${TMP_DIR}/repo/${MANIFEST_NAME}"

if grep -q '^version https://git-lfs.github.com/spec/v1$' "${ONNX_SRC}" 2>/dev/null; then
    echo "ERROR: model.onnx is still a Git LFS pointer" >&2
    exit 1
fi
if [ ! -s "${ONNX_SRC}" ]; then
    echo "ERROR: model.onnx is empty" >&2
    exit 1
fi
if [ ! -f "${META_SRC}" ]; then
    echo "ERROR: ${META_NAME} not found in ${REPO}@${VERSION}" >&2
    exit 1
fi

# --- Validate the publisher checksum manifest. ---
[ -s "${MANIFEST_SRC}" ] || {
    echo "ERROR: ${MANIFEST_NAME} not found in ${REPO}@${VERSION}" >&2
    exit 1
}

manifest_names="$(awk '{print $2}' "${MANIFEST_SRC}" | LC_ALL=C sort)"
expected_names="$(printf '%s\n' model.onnx model.yaml | LC_ALL=C sort)"
if [ "${manifest_names}" != "${expected_names}" ]; then
    echo "ERROR: ${MANIFEST_NAME} must contain exactly model.onnx and model.yaml" >&2
    exit 1
fi

if ! awk 'NF == 2 && length($1) == 64 && $1 ~ /^[0-9a-f]+$/ && \
    ($2 == "model.onnx" || $2 == "model.yaml") { next } { exit 1 }' \
    "${MANIFEST_SRC}"; then
    echo "ERROR: malformed ${MANIFEST_NAME}" >&2
    exit 1
fi

if ! (cd "${TMP_DIR}/repo" && sha256sum --check --strict SHA256SUMS); then
    echo "ERROR: model release checksum verification failed" >&2
    exit 1
fi

# --- Acquisition lock lives beside the model root, outside the model tree. ---
mkdir -p "$(dirname "${MODEL_ROOT}")"
if ! command -v flock >/dev/null 2>&1; then
    echo "ERROR: flock is required for atomic model installation" >&2
    exit 1
fi
exec 9>"${MODEL_ROOT}.fetch.lock"
if ! flock -n 9; then
    echo "ERROR: another model fetch is active: ${MODEL_ROOT}.fetch.lock" >&2
    exit 1
fi
mkdir -p "${MODEL_ROOT}/releases"

STAGING_DIR="$(mktemp -d "${MODEL_ROOT}/.staging.${VERSION}.XXXXXX")"
CURRENT_TMP=""
RELEASE_DIR="${MODEL_ROOT}/releases/${VERSION}"
RELEASE_CREATED=0
CURRENT_COMMITTED=0

install -m 0644 "${ONNX_SRC}" "${STAGING_DIR}/model.onnx"
install -m 0644 "${META_SRC}" "${STAGING_DIR}/model.yaml"
install -m 0644 "${MANIFEST_SRC}" "${STAGING_DIR}/SHA256SUMS"
(cd "${STAGING_DIR}" && sha256sum --check --strict SHA256SUMS)

if [ -e "${RELEASE_DIR}" ]; then
    if (cd "${RELEASE_DIR}" && sha256sum --check --strict SHA256SUMS >/dev/null 2>&1) && \
       cmp -s "${RELEASE_DIR}/SHA256SUMS" "${STAGING_DIR}/SHA256SUMS"; then
        echo "Release ${VERSION} is already installed and verified"
    else
        echo "ERROR: installed immutable tag ${VERSION} differs from HF" >&2
        exit 1
    fi
else
    RELEASE_CREATED=1
    mv "${STAGING_DIR}" "${RELEASE_DIR}"
    STAGING_DIR=""
fi

# --- Commit point: atomic symlink replacement with signals ignored. ---
CURRENT_TMP="${MODEL_ROOT}/.current.${VERSION}.$$"
ln -s "releases/${VERSION}" "${CURRENT_TMP}"
trap '' INT TERM
if mv -Tf "${CURRENT_TMP}" "${MODEL_ROOT}/current"; then
    CURRENT_TMP=""
    CURRENT_COMMITTED=1
else
    trap 'on_signal 130' INT
    trap 'on_signal 143' TERM
    echo "ERROR: unable to update current model release" >&2
    exit 1
fi
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

echo "Fetched release ${REPO}@${VERSION}:"
echo "  ${MODEL_ROOT}/current/model.onnx"
echo "  ${MODEL_ROOT}/current/model.yaml"
echo "  ${MODEL_ROOT}/current/SHA256SUMS"
git -C "${TMP_DIR}/repo" rev-parse HEAD