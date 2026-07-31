#!/usr/bin/env bash
# Deploy the cross-compiled binary to Manifold 3 and optionally run it.
#
# Usage:
#   scripts/deploy.sh <manifold3-ip> [--no-build] [run] [-- <app args>]
#
# Examples:
#   scripts/deploy.sh 192.168.42.120               # build + deploy (no run)
#   scripts/deploy.sh 192.168.42.120 run           # build + deploy + run foreground
#   scripts/deploy.sh 192.168.42.120 --no-build run  # skip build, deploy + run
#
# The remote working directory is ~/vision-detect on the target. In run mode
# the remote exit code is propagated (ssh exits with the remote command's
# status).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
BUILD_DIR="${REPO_ROOT}/build-cross"
BIN_PATH="src/app/manifold3_vision_detect"
REMOTE_DIR="~/vision-detect"
REMOTE_BIN="${REMOTE_DIR}/manifold3_vision_detect"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)

usage() {
    sed -n '2,12p' "${BASH_SOURCE[0]}"
    exit 1
}

# --- Argument parsing ---
TARGET_IP=""
DO_BUILD=true
DO_RUN=false
APP_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build)
            DO_BUILD=false
            shift
            ;;
        run)
            DO_RUN=true
            shift
            ;;
        --)
            shift
            APP_ARGS=("$@")
            break
            ;;
        -*)
            usage
            ;;
        *)
            if [ -z "${TARGET_IP}" ]; then
                TARGET_IP="$1"
                shift
            else
                usage
            fi
            ;;
    esac
done

if [ -z "${TARGET_IP}" ]; then
    usage
fi

# --- Build ---
if [ "${DO_BUILD}" = true ]; then
    # shellcheck source=scripts/setup_env.sh
    source "${REPO_ROOT}/scripts/setup_env.sh"
    if [ ! -d "${BUILD_DIR}" ]; then
        echo "Build directory missing; configure first with:"
        echo "  cmake --preset manifold3-cross-release"
        exit 1
    fi
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

BIN_PATH_FULL="${BUILD_DIR}/${BIN_PATH}"
if [ ! -f "${BIN_PATH_FULL}" ]; then
    echo "Binary not found: ${BIN_PATH_FULL}"
    exit 1
fi

# --- Deploy ---
echo "Deploying to ${TARGET_IP}:${REMOTE_BIN}"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "mkdir -p ${REMOTE_DIR}" >/dev/null
scp "${SSH_OPTS[@]}" "${BIN_PATH_FULL}" "dji@${TARGET_IP}:${REMOTE_BIN}"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "chmod +x ${REMOTE_BIN}"

# --- Run ---
if [ "${DO_RUN}" = true ]; then
    # -t allocates a tty so Ctrl-C reaches the remote process; the ssh exit
    # status is the remote command's exit status.
    REMOTE_CMD="cd ${REMOTE_DIR} && exec ./manifold3_vision_detect"
    if [ ${#APP_ARGS[@]} -gt 0 ]; then
        REMOTE_CMD="${REMOTE_CMD} $(printf '%q ' "${APP_ARGS[@]}")"
    fi
    exec ssh -t "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "${REMOTE_CMD}"
fi
