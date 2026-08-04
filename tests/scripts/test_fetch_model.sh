#!/usr/bin/env bash
# Host tests for scripts/fetch_model.sh.
#
# The script under test is driven with a fake `git` (see
# tests/scripts/fixtures/fake_git) that clones a fixture model tree instead of
# touching the HF network. No HF access or device is needed.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${REPO_ROOT}/scripts/fetch_model.sh"
FIXTURES="$(cd "$(dirname "${BASH_SOURCE[0]}")/fixtures" && pwd)"
MAKE_MODEL="${FIXTURES}/make_fake_model.sh"

CASE_NAME=""
LAST_RC=0
LAST_OUT=""
LAST_ERR=""

fail() {
    echo "FAIL [${CASE_NAME}]: $1" >&2
    exit 1
}

# Build the fake model tree (model.onnx + model.yaml) once.
MODEL_TREE="$(mktemp -d)"
bash "${MAKE_MODEL}" "${MODEL_TREE}"

# run_script [script args...]
run_script() {
    local outdir errdir
    outdir="$(mktemp -d)"
    errdir="$(mktemp -d)"
    LAST_OUT="${outdir}/out.log"
    LAST_ERR="${errdir}/err.log"
    set +e
    PATH="${FIXTURES}:${PATH}" \
        FAKE_GIT_MODEL_TREE="${MODEL_TREE}" \
        "${SCRIPT}" "$@" >"${LAST_OUT}" 2>"${LAST_ERR}"
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

# --- Case 1: happy path fetches onnx + yaml ---
test_happy_path() {
    CASE_NAME="happy path"
    local out
    out="$(mktemp -d)/models"
    run_script --out "${out}"
    expect_rc 0
    [ -f "${out}/model.onnx" ] || fail "model.onnx not fetched"
    [ -f "${out}/model.yaml" ] || fail "model.yaml not fetched"
    expect_out_contains "model.onnx"
    expect_out_contains "model.yaml"
    expect_out_contains "SHA256"
    echo "PASS: happy path fetches onnx + yaml and prints sha256"
}

# --- Case 2: default out dir is .local/models under repo root ---
test_default_out() {
    CASE_NAME="default out dir"
    local out="${REPO_ROOT}/.local/models"
    run_script
    expect_rc 0
    [ -f "${out}/model.onnx" ] || fail "default out model.onnx missing"
    [ -f "${out}/model.yaml" ] || fail "default out model.yaml missing"
    echo "PASS: default out dir is .local/models"
}

# --- Case 3: --version is accepted and forwarded ---
test_version_forwarded() {
    CASE_NAME="--version forwarded"
    local out
    out="$(mktemp -d)/models"
    run_script --version v1.0.0 --repo zyzh0/tree-crown-yolo11-seg --out "${out}"
    expect_rc 0
    [ -f "${out}/model.yaml" ] || fail "model.yaml not fetched"
    echo "PASS: --version accepted without error"
}

# --- Case 4: missing onnx in repo ---
test_missing_onnx() {
    CASE_NAME="missing onnx"
    local out
    out="$(mktemp -d)/models"
    run_script --out "${out}" --repo missing/onnx
    expect_rc 1
    expect_err_contains "model.onnx"
    echo "PASS: missing onnx exits 1"
}

# --- Case 5: missing yaml in repo ---
test_missing_yaml() {
    CASE_NAME="missing yaml"
    local out
    out="$(mktemp -d)/models"
    run_script --out "${out}" --repo missing/yaml
    expect_rc 1
    expect_err_contains "model.yaml"
    echo "PASS: missing yaml exits 1"
}

# --- Case 6: unknown option exits 1 ---
test_unknown_option() {
    CASE_NAME="unknown option"
    run_script --bogus
    expect_rc 1
    expect_out_contains "Usage"
    echo "PASS: unknown option exits 1 with usage"
}

test_happy_path
test_default_out
test_version_forwarded
test_missing_onnx
test_missing_yaml
test_unknown_option
echo "ALL 6 fetch_model_script cases passed"