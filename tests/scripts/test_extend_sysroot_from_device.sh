#!/usr/bin/env bash
# Host tests for scripts/extend_sysroot_from_device.sh.
#
# The script under test is driven with fake ssh/scp executables (see
# tests/scripts/fixtures/) that answer dpkg-query from a fixture file, so no
# device is needed. `--no-verify` skips the final check_inference_sysroot.sh
# run, which requires a fully populated sysroot (the checker itself is
# covered by its own tests in Task 3).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${REPO_ROOT}/scripts/extend_sysroot_from_device.sh"
FIXTURES="$(cd "$(dirname "${BASH_SOURCE[0]}")/fixtures" && pwd)"
EXPECTED_PKGS_FILE="${FIXTURES}/expected_packages.txt"

CASE_NAME=""
LAST_RC=0
LAST_CALLS=""
LAST_OUT=""
LAST_ERR=""

fail() {
    echo "FAIL [${CASE_NAME}]: $1" >&2
    exit 1
}

# run_script [--missing "pkg..."] [--fixture FILE] [script args...]
#
# Runs the script under test with the fake ssh/scp in PATH. Both fake
# executables log to the same file (FAKE_SSH_LOG = FAKE_SCP_LOG), so the
# relative order of ssh and scp calls is observable. Results land in
# LAST_RC / LAST_CALLS / LAST_OUT / LAST_ERR.
run_script() {
    local missing=""
    local fixture="${EXPECTED_PKGS_FILE}"
    while [ $# -gt 0 ]; do
        case "$1" in
            --missing)
                missing="$2"
                shift 2
                ;;
            --fixture)
                fixture="$2"
                shift 2
                ;;
            *)
                break
                ;;
        esac
    done

    LAST_TMP="$(mktemp -d)"
    LAST_CALLS="${LAST_TMP}/calls.log"
    LAST_OUT="${LAST_TMP}/out.log"
    LAST_ERR="${LAST_TMP}/err.log"
    set +e
    PATH="${FIXTURES}:${PATH}" \
        FAKE_SSH_LOG="${LAST_CALLS}" \
        FAKE_SCP_LOG="${LAST_CALLS}" \
        FAKE_PKG_QUERY_FILE="${fixture}" \
        FAKE_MISSING_PKGS="${missing}" \
        env -u MANIFOLD3_SYSROOT "${SCRIPT}" "$@" >"${LAST_OUT}" 2>"${LAST_ERR}"
    LAST_RC=$?
    set -e
}

expect_rc() {
    [ "${LAST_RC}" -eq "$1" ] || fail "expected exit $1, got ${LAST_RC}"
}

expect_stderr_contains() {
    grep -q "$1" "${LAST_ERR}" || fail "stderr missing '$1': $(tr '\n' ' ' <"${LAST_ERR}")"
}

expect_scp_calls() {
    local count
    count="$(grep -c '^SCP:' "${LAST_CALLS}" 2>/dev/null || true)"
    [ "${count}" -eq "$1" ] || fail "expected $1 scp calls, got ${count}"
}

# --- Case 1: happy path ---
test_happy_path() {
    CASE_NAME="happy path"
    run_script --sysroot "$(mktemp -d)/sysroot" --no-verify
    expect_rc 0
    local dpkg_count scp_count first_scp last_dpkg
    dpkg_count="$(grep -c 'dpkg-query' "${LAST_CALLS}" || true)"
    [ "${dpkg_count}" -eq 14 ] || fail "expected 14 package queries, got ${dpkg_count}"
    while read -r pkg _; do
        grep -q "${pkg}" "${LAST_CALLS}" || fail "package ${pkg} was not queried"
    done <"${EXPECTED_PKGS_FILE}"
    expect_scp_calls 4
    first_scp="$(grep -n '^SCP:' "${LAST_CALLS}" | head -1 | cut -d: -f1)"
    last_dpkg="$(grep -n 'dpkg-query' "${LAST_CALLS}" | tail -1 | cut -d: -f1)"
    [ "${first_scp}" -gt "${last_dpkg}" ] || fail "scp ran before all package checks"
    grep -q '^DONE:' "${LAST_OUT}" || fail "missing final DONE line"
    echo "PASS: happy path (14 exact version checks, then 4 scp calls)"
}

# --- Case 2: missing package ---
test_missing_package() {
    CASE_NAME="missing package"
    run_script --missing "libnvinfer-dev" --sysroot "$(mktemp -d)/sysroot" --no-verify
    expect_rc 1
    expect_scp_calls 0
    expect_stderr_contains "missing"
    echo "PASS: missing package exits 1 before any scp"
}

# --- Case 3: version mismatch ---
test_version_mismatch() {
    CASE_NAME="version mismatch"
    local tmp wrong
    tmp="$(mktemp -d)"
    wrong="${tmp}/wrong_packages.txt"
    sed 's/^libcudnn8 .*/libcudnn8 9.9.9-1+cuda11.4/' "${EXPECTED_PKGS_FILE}" >"${wrong}"
    run_script --fixture "${wrong}" --sysroot "$(mktemp -d)/sysroot" --no-verify
    expect_rc 1
    expect_scp_calls 0
    expect_stderr_contains "!= expected"
    expect_stderr_contains "libcudnn8"
    echo "PASS: version mismatch exits 1 before any scp"
}

# --- Case 4: --sysroot without a value ---
test_sysroot_missing_value() {
    CASE_NAME="--sysroot without value"
    run_script --sysroot
    expect_rc 2
    expect_stderr_contains "usage"
    echo "PASS: --sysroot without value exits 2 with usage"
}

# --- Case 5: two positional arguments ---
test_two_positionals() {
    CASE_NAME="two positional IPs"
    run_script 192.168.42.1 10.0.0.2
    expect_rc 2
    expect_stderr_contains "usage"
    echo "PASS: second positional argument exits 2 with usage"
}

# --- Case 6: unknown option ---
test_unknown_option() {
    CASE_NAME="unknown option"
    run_script --bogus
    expect_rc 2
    expect_stderr_contains "usage"
    echo "PASS: unknown option exits 2 with usage"
}

test_happy_path
test_missing_package
test_version_mismatch
test_sysroot_missing_value
test_two_positionals
test_unknown_option
echo "ALL 6 sysroot_extend_script cases passed"
