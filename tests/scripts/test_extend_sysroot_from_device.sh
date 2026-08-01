#!/usr/bin/env bash
# Host tests for scripts/extend_sysroot_from_device.sh.
#
# The script under test is driven with fake ssh/scp executables (see
# tests/scripts/fixtures/) that answer dpkg-query from a fixture file and
# copy from a fake device tree (fixtures/make_fake_sysroot.sh), so no device
# is needed. `--no-verify` skips the check_inference_sysroot.sh runs (the
# checker itself is covered by test_check_inference_sysroot.sh).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${REPO_ROOT}/scripts/extend_sysroot_from_device.sh"
FIXTURES="$(cd "$(dirname "${BASH_SOURCE[0]}")/fixtures" && pwd)"
EXPECTED_PKGS_FILE="${FIXTURES}/expected_packages.txt"
MAKE_FAKE="${FIXTURES}/make_fake_sysroot.sh"

CASE_NAME=""
LAST_RC=0
LAST_CALLS=""
LAST_OUT=""
LAST_ERR=""

fail() {
    echo "FAIL [${CASE_NAME}]: $1" >&2
    exit 1
}

# Build the fake device tree once: the same tree serves as the remote side
# of every fake scp call.
DEVICE_TREE="$(mktemp -d)"
bash "${MAKE_FAKE}" "${DEVICE_TREE}"

# run_script [--missing "pkg..."] [--fixture FILE] [--sleep SECS] [script args...]
#
# Runs the script under test with the fake ssh/scp in PATH. Both fake
# executables log to the same file (FAKE_SSH_LOG = FAKE_SCP_LOG), so the
# relative order of ssh and scp calls is observable. Results land in
# LAST_RC / LAST_CALLS / LAST_OUT / LAST_ERR.
run_script() {
    local missing=""
    local fixture="${EXPECTED_PKGS_FILE}"
    local sleep_secs=""
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
            --sleep)
                sleep_secs="$2"
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
        FAKE_DEVICE_ROOT="${DEVICE_TREE}" \
        FAKE_SCP_SLEEP="${sleep_secs}" \
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

expect_out_contains() {
    grep -q "$1" "${LAST_OUT}" || fail "stdout missing '$1': $(tr '\n' ' ' <"${LAST_OUT}")"
}

expect_scp_calls() {
    local count
    count="$(grep -c '^SCP:' "${LAST_CALLS}" 2>/dev/null || true)"
    [ "${count}" -eq "$1" ] || fail "expected $1 scp calls, got ${count}"
}

# Snapshot of every managed file (name, type, size, link target) under the
# sysroot, sorted for comparison.
snapshot_managed() {
    local sysroot="$1"
    (cd "${sysroot}" && find \
        usr/include/aarch64-linux-gnu usr/local/cuda usr/lib/aarch64-linux-gnu \
        \( -name 'NvInfer*.h' -o -name 'NvOnnx*.h' -o -name 'libnvinfer*' \
        -o -name 'libnvonnxparser*' -o -name 'libnvinfer_plugin*' \
        -o -name 'libcudnn.so*' -o -name 'libcudart*' -o -name 'libcudla*' \
        -o -name 'libcublas*' -o -name 'libcublasLt*' \) \
        -printf '%P %y %s %l\n' | sort)
}

# --- Case 1: happy path with verification ---
test_happy_path() {
    CASE_NAME="happy path"
    run_script --sysroot "$(mktemp -d)/sysroot"
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
    expect_out_contains "PASS: inference sysroot extension present"
    expect_out_contains '^DONE: sysroot extension applied to '
    echo "PASS: happy path (14 exact version checks, 4 scp calls, staged install, PASS + DONE)"
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

# --- Case 7: rerun is idempotent for managed files ---
test_rerun_idempotent() {
    CASE_NAME="rerun idempotent"
    local sysroot before after
    sysroot="$(mktemp -d)/sysroot"
    run_script --sysroot "${sysroot}"
    expect_rc 0
    [ -f "${sysroot}/usr/include/aarch64-linux-gnu/NvInfer.h" ] || fail "NvInfer.h not installed"
    [ -f "${sysroot}/usr/local/cuda/include/cuda_runtime.h" ] || fail "cuda_runtime.h not installed"
    [ -f "${sysroot}/usr/lib/aarch64-linux-gnu/libnvinfer.so.8.5.2" ] || fail "libnvinfer real file missing"
    [ -L "${sysroot}/usr/lib/aarch64-linux-gnu/libnvinfer.so" ] || fail "libnvinfer.so not a symlink"
    [ "$(readlink "${sysroot}/usr/lib/aarch64-linux-gnu/libnvinfer.so")" = "libnvinfer.so.8.5.2" ] \
        || fail "libnvinfer.so wrong target"
    [ -L "${sysroot}/usr/lib/aarch64-linux-gnu/libcudnn.so.8" ] || fail "libcudnn.so.8 not a symlink"
    [ ! -e "${sysroot}/usr/lib/aarch64-linux-gnu/libcudnn.so" ] && \
        [ ! -L "${sysroot}/usr/lib/aarch64-linux-gnu/libcudnn.so" ] \
        || fail "libcudnn.so must not exist"
    before="$(snapshot_managed "${sysroot}")"
    run_script --sysroot "${sysroot}"
    expect_rc 0
    after="$(snapshot_managed "${sysroot}")"
    if [ "${before}" != "${after}" ]; then
        diff <(printf '%s\n' "${before}") <(printf '%s\n' "${after}") >&2 || true
        fail "managed file tree changed on rerun"
    fi
    echo "PASS: rerun is idempotent for managed files"
}

# --- Case 8: interruption mid-copy leaves the sysroot untouched ---
#
# The script under test runs in its own session (start_new_session) so its
# SIGINT disposition is the default, not SIG_IGN: background jobs started
# with `&` from a non-interactive shell ignore SIGINT, which would silently
# swallow the interrupt. SIGINT is then sent to the whole process group,
# like Ctrl-C in a terminal.
test_interrupted_copy() {
    CASE_NAME="interrupt during copy"
    local sysroot calls out err rc
    sysroot="$(mktemp -d)/sysroot"
    calls="$(mktemp -d)/calls.log"
    out="$(mktemp -d)/out.log"
    err="$(mktemp -d)/err.log"
    rc="$(FAKE_SCP_SLEEP=8 python3 - \
        "${SCRIPT}" "${sysroot}" "${FIXTURES}" "${EXPECTED_PKGS_FILE}" \
        "${DEVICE_TREE}" "${calls}" "${out}" "${err}" <<'PYEOF'
import os
import signal
import subprocess
import sys
import time

script, sysroot, fixtures, pkg_file, device_tree, calls, out, err = sys.argv[1:]
env = os.environ.copy()
env["PATH"] = f"{fixtures}:{env.get('PATH', '')}"
env["FAKE_SSH_LOG"] = calls
env["FAKE_SCP_LOG"] = calls
env["FAKE_PKG_QUERY_FILE"] = pkg_file
env["FAKE_DEVICE_ROOT"] = device_tree
env.pop("MANIFOLD3_SYSROOT", None)
with open(out, "w") as out_f, open(err, "w") as err_f:
    proc = subprocess.Popen(
        ["bash", script, "--sysroot", sysroot],
        env=env, stdout=out_f, stderr=err_f, start_new_session=True,
    )
    for _ in range(200):
        try:
            if "SCP-DONE:" in open(calls).read():
                break
        except OSError:
            pass
        time.sleep(0.1)
    else:
        print("NO_SCP")
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait()
        sys.exit(3)
    os.killpg(proc.pid, signal.SIGINT)
    print(proc.wait())
PYEOF
)"
    [ "${rc}" = "NO_SCP" ] && fail "scp never started"
    [ "${rc}" -eq 0 ] && fail "interrupted run exited 0"
    if ls -d "${sysroot}"/.sysroot-staging.* >/dev/null 2>&1; then
        fail "staging directory not cleaned up"
    fi
    [ ! -e "${sysroot}/usr/include/aarch64-linux-gnu/NvInfer.h" ] \
        || fail "partial copy left NvInfer.h in the sysroot"
    [ ! -e "${sysroot}/usr/lib/aarch64-linux-gnu/libnvinfer.so" ] \
        || fail "partial copy left libnvinfer.so in the sysroot"
    echo "PASS: interrupt mid-copy leaves the sysroot untouched"
}

test_happy_path
test_missing_package
test_version_mismatch
test_sysroot_missing_value
test_two_positionals
test_unknown_option
test_rerun_idempotent
test_interrupted_copy
echo "ALL 8 sysroot_extend_script cases passed"
