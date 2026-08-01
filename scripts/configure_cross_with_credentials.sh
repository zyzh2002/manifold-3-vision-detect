#!/usr/bin/env bash
# Configure the cross build with local development credentials.
#
# Reads .local/credentials.env (git-ignored, never committed) and configures
# the manifold3-cross-release preset with those values. When the file is
# missing or the values are placeholders, configures with the defaults and
# the application will reject them at startup (expected during development
# before real credentials exist).
#
# Usage:
#   scripts/configure_cross_with_credentials.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CRED_FILE="${REPO_ROOT}/.local/credentials.env"

CMAKE_ARGS=(--preset manifold3-cross-release)

if [ -f "${CRED_FILE}" ]; then
    # shellcheck disable=SC1090
    source "${CRED_FILE}"
    CMAKE_ARGS+=(
        -DMANIFOLD3_APP_ID="${MANIFOLD3_APP_ID}"
        -DMANIFOLD3_APP_KEY="${MANIFOLD3_APP_KEY}"
        -DMANIFOLD3_APP_LICENSE="${MANIFOLD3_APP_LICENSE}"
        -DMANIFOLD3_APP_NAME="${MANIFOLD3_APP_NAME}"
        -DMANIFOLD3_DEVELOPER_ACCOUNT="${MANIFOLD3_DEVELOPER_ACCOUNT}"
        -DMANIFOLD3_BAUD_RATE="${MANIFOLD3_BAUD_RATE:-460800}"
    )
    echo "Using credentials from ${CRED_FILE}"
else
    echo "WARNING: ${CRED_FILE} not found; configuring with placeholder defaults" >&2
fi

cmake "${CMAKE_ARGS[@]}"
