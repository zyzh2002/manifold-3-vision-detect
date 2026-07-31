#!/usr/bin/env bash
# Target-side smoke test: loads the dummy engine and runs one inference.
# Usage: scripts/run_inference_smoke.sh <manifold3-ip> [--no-build]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
REMOTE_DIR="~/vision-detect"
REMOTE_BIN="${REMOTE_DIR}/inference_smoke"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)

TARGET_IP="$1"
DO_BUILD=true
[ "${2:-}" = "--no-build" ] && DO_BUILD=false

if [ "${DO_BUILD}" = true ]; then
    source "${REPO_ROOT}/scripts/setup_env.sh"
    cmake --build "${REPO_ROOT}/build-cross" --target inference_smoke -j"$(nproc)"
fi

scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build-cross/src/inference/inference_smoke" "dji@${TARGET_IP}:${REMOTE_BIN}"
scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build/dummy_yolo11_seg.engine" "dji@${TARGET_IP}:${REMOTE_DIR}/dummy_yolo11_seg.engine"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "chmod +x ${REMOTE_BIN} && ${REMOTE_BIN} ${REMOTE_DIR}/dummy_yolo11_seg.engine"
