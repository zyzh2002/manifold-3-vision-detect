#!/usr/bin/env bash
# Source this script to set up the Manifold 3 cross-compilation environment.
# Usage: source scripts/setup_env.sh
#
# Exports MANIFOLD3_TOOLCHAIN_DIR and MANIFOLD3_SYSROOT with repository-local
# defaults. Pre-existing values are preserved.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export MANIFOLD3_TOOLCHAIN_DIR="${MANIFOLD3_TOOLCHAIN_DIR:-$REPO_ROOT/.local-toolchains/bootlin-gcc-9.3-nvidia}"
export MANIFOLD3_SYSROOT="${MANIFOLD3_SYSROOT:-$REPO_ROOT/sysroot}"

echo "MANIFOLD3_TOOLCHAIN_DIR=$MANIFOLD3_TOOLCHAIN_DIR"
echo "MANIFOLD3_SYSROOT=$MANIFOLD3_SYSROOT"
