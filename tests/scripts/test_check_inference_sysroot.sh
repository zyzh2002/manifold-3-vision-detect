#!/usr/bin/env bash
# Host tests for scripts/check_inference_sysroot.sh.
#
# Builds a complete fake Phase 5 sysroot extension
# (fixtures/make_fake_sysroot.sh) and drives the checker against it and
# against deliberately broken copies: empty explicit sysroot, plain file
# instead of symlink, wrong readlink target, dangling link chain, a present
# libcudnn.so (including a dangling one), wrong ELF machine, and wrong
# SONAME.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECKER="${REPO_ROOT}/scripts/check_inference_sysroot.sh"
FIXTURES="$(cd "$(dirname "${BASH_SOURCE[0]}")/fixtures" && pwd)"
MAKE_FAKE="${FIXTURES}/make_fake_sysroot.sh"
MAKE_ELF="${FIXTURES}/make_fake_elf.py"

CASE_NAME=""
LAST_RC=0
LAST_OUT=""
LAST_ERR=""

fail() {
    echo "FAIL [${CASE_NAME}]: $1" >&2
    exit 1
}

# run_checker [--env SYSROOT] [checker args...]
#
# Runs the checker with MANIFOLD3_SYSROOT optionally set and records
# LAST_RC / LAST_OUT / LAST_ERR.
run_checker() {
    local env_sysroot=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --env)
                env_sysroot="$2"
                shift 2
                ;;
            *)
                break
                ;;
        esac
    done
    LAST_TMP="$(mktemp -d)"
    LAST_OUT="${LAST_TMP}/out.log"
    LAST_ERR="${LAST_TMP}/err.log"
    set +e
    if [ -n "${env_sysroot}" ]; then
        MANIFOLD3_SYSROOT="${env_sysroot}" bash "${CHECKER}" "$@" \
            >"${LAST_OUT}" 2>"${LAST_ERR}"
    else
        env -u MANIFOLD3_SYSROOT bash "${CHECKER}" "$@" \
            >"${LAST_OUT}" 2>"${LAST_ERR}"
    fi
    LAST_RC=$?
    set -e
}

expect_rc() {
    [ "${LAST_RC}" -eq "$1" ] || fail "expected exit $1, got ${LAST_RC}"
}

expect_out_contains() {
    grep -q "$1" "${LAST_OUT}" || fail "stdout missing '$1': $(tr '\n' ' ' <"${LAST_OUT}")"
}

expect_err_contains() {
    grep -q "$1" "${LAST_ERR}" || fail "stderr missing '$1': $(tr '\n' ' ' <"${LAST_ERR}")"
}

new_sysroot() {
    local root
    root="$(mktemp -d)"
    bash "${MAKE_FAKE}" "${root}"
    echo "${root}"
}

# --- Case 1: happy path ---
test_happy_path() {
    CASE_NAME="happy path"
    local root
    root="$(new_sysroot)"
    run_checker --sysroot "${root}"
    expect_rc 0
    expect_out_contains "PASS: inference sysroot extension present"
    echo "PASS: complete fake sysroot passes"
}

# --- Case 2: empty explicit sysroot fails even when the default is complete ---
test_empty_explicit_sysroot() {
    CASE_NAME="empty explicit sysroot"
    local root empty
    root="$(new_sysroot)"
    empty="$(mktemp -d)"
    run_checker --env "${root}" --sysroot "${empty}"
    expect_rc 1
    echo "PASS: empty explicit --sysroot fails while the default is complete"
}

# --- Case 3: plain file instead of symlink fails ---
test_plain_file_instead_of_symlink() {
    CASE_NAME="plain file instead of symlink"
    local root
    root="$(new_sysroot)"
    rm -f "${root}/usr/lib/aarch64-linux-gnu/libnvinfer.so"
    touch "${root}/usr/lib/aarch64-linux-gnu/libnvinfer.so"
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "libnvinfer.so"
    echo "PASS: regular file in place of libnvinfer.so symlink fails"
}

# --- Case 4: wrong readlink target fails ---
test_wrong_readlink() {
    CASE_NAME="wrong readlink target"
    local root
    root="$(new_sysroot)"
    ln -sfn libcudart.so.11.6.6.84 "${root}/usr/local/cuda/lib64/libcudart.so.11.0"
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "libcudart.so.11.0"
    expect_err_contains "unexpected readlink"
    echo "PASS: wrong libcudart.so.11.0 readlink target fails"
}

# --- Case 5: dangling link chain fails ---
test_dangling_link() {
    CASE_NAME="dangling link chain"
    local root
    root="$(new_sysroot)"
    rm -f "${root}/usr/local/cuda/lib64/libcublasLt.so.11.6.6.84"
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "libcublasLt.so.11.6.6.84"
    echo "PASS: dangling libcublasLt chain fails"
}

# --- Case 6: libcudnn.so must not exist (regular file) ---
test_libcudnn_so_present() {
    CASE_NAME="libcudnn.so regular file"
    local root
    root="$(new_sysroot)"
    touch "${root}/usr/lib/aarch64-linux-gnu/libcudnn.so"
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "libcudnn.so must not exist"
    echo "PASS: regular libcudnn.so fails"
}

# --- Case 7: libcudnn.so must not exist (dangling symlink) ---
test_libcudnn_so_dangling() {
    CASE_NAME="libcudnn.so dangling symlink"
    local root
    root="$(new_sysroot)"
    ln -s libcudnn.so.9.9.9 "${root}/usr/lib/aarch64-linux-gnu/libcudnn.so"
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "libcudnn.so must not exist"
    echo "PASS: dangling libcudnn.so symlink fails"
}

# --- Case 8: non-AArch64 ELF fails ---
test_wrong_machine() {
    CASE_NAME="non-AArch64 ELF"
    local root
    root="$(new_sysroot)"
    python3 "${MAKE_ELF}" "${root}/usr/lib/aarch64-linux-gnu/libnvinfer.so.8.5.2" \
        libnvinfer.so.8 62
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "not an AArch64 ELF"
    echo "PASS: x86_64 machine in place of AArch64 fails"
}

# --- Case 9: wrong SONAME fails ---
test_wrong_soname() {
    CASE_NAME="wrong SONAME"
    local root
    root="$(new_sysroot)"
    python3 "${MAKE_ELF}" "${root}/usr/local/cuda/lib64/libcudart.so.11.4.298" \
        libcudart.so.11.9
    run_checker --sysroot "${root}"
    expect_rc 1
    expect_err_contains "unexpected SONAME"
    expect_err_contains "libcudart.so.11.4.298"
    echo "PASS: wrong libcudart SONAME fails"
}

# --- Case 10: --sysroot without a value ---
test_sysroot_missing_value() {
    CASE_NAME="--sysroot without value"
    run_checker --sysroot
    expect_rc 2
    expect_err_contains "usage"
    echo "PASS: --sysroot without value exits 2 with usage"
}

test_happy_path
test_empty_explicit_sysroot
test_plain_file_instead_of_symlink
test_wrong_readlink
test_dangling_link
test_libcudnn_so_present
test_libcudnn_so_dangling
test_wrong_machine
test_wrong_soname
test_sysroot_missing_value
echo "ALL 10 sysroot_checker cases passed"
