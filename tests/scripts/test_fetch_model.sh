#!/usr/bin/env bash
# Host tests for scripts/fetch_model.sh.
#
# The script under test is driven with a fake `git` (see
# tests/scripts/fixtures/git) that serves a fixture model tree instead of
# touching the HF network. Tests use an isolated MANIFOLD3_MODEL_ROOT below an
# owned temporary root and never touch the real .local/models tree.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="${REPO_ROOT}/scripts/fetch_model.sh"
FIXTURES="$(cd "$(dirname "${BASH_SOURCE[0]}")/fixtures" && pwd)"
MAKE_MODEL="${FIXTURES}/make_fake_model.sh"

CASE_NAME=""
LAST_RC=0
LAST_OUT=""
LAST_ERR=""
MODEL_ROOT=""

fail() {
    echo "FAIL [${CASE_NAME}]: $1" >&2
    exit 1
}

# One owned root; every case and artifact lives below it.
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "${TEST_ROOT}"' EXIT
MODEL_TREE="${TEST_ROOT}/model-source"
mkdir -p "${MODEL_TREE}"
bash "${MAKE_MODEL}" "${MODEL_TREE}"

# run_script [script args...]. MODEL_ROOT is a global; each test sets it to a
# fresh isolated root before calling run_script.
run_script() {
    local case_root fake_ctrl env_args=()
    case_root="${TEST_ROOT}/case-${CASE_NAME// /-}"
    mkdir -p "${case_root}"
    [ -n "${MODEL_ROOT}" ] || MODEL_ROOT="${case_root}/models"
    LAST_OUT="${case_root}/stdout.log"
    LAST_ERR="${case_root}/stderr.log"
    # Forward all FAKE_* controls from the caller.
    for fake_ctrl in $(env | sed -n 's/^\(FAKE_[A-Z0-9_]*\)=.*/\1/p'); do
        env_args+=("${fake_ctrl}=${!fake_ctrl}")
    done
    set +e
    PATH="${FIXTURES}:${PATH}" \
        FAKE_GIT_MODEL_TREE="${MODEL_TREE}" \
        MANIFOLD3_MODEL_ROOT="${MODEL_ROOT}" \
        env "${env_args[@]}" \
        "${SCRIPT}" "$@" >"${LAST_OUT}" 2>"${LAST_ERR}"
    LAST_RC=$?
    set -e
}

expect_rc() {
    [ "${LAST_RC}" -eq "$1" ] || fail "expected exit $1, got ${LAST_RC} (stderr: $(tr '\n' ' ' <"${LAST_ERR}"))"
}

expect_out_contains() {
    grep -q -- "$1" "${LAST_OUT}" || fail "stdout missing '$1': $(tr '\n' ' ' <"${LAST_OUT}")"
}

expect_err_contains() {
    grep -q -- "$1" "${LAST_ERR}" || fail "stderr missing '$1': $(tr '\n' ' ' <"${LAST_ERR}")"
}

snapshot_models() {
    if [ ! -d "${REPO_ROOT}/.local/models" ]; then
        printf 'ABSENT\n'
        return
    fi
    tar --sort=name --mtime='UTC 1970-01-01' \
        -cf - -C "${REPO_ROOT}/.local" models | sha256sum
}
# --- Task 1: isolation ---
test_sandbox_reproduces_original_fixed_root() {
    CASE_NAME="red sandbox fixed root"
    local red_repo red_rc
    red_repo="${TEST_ROOT}/red-repo"
    mkdir -p "${red_repo}/scripts"
    git -C "${REPO_ROOT}" show \
        "$(git -C "${REPO_ROOT}" log --format=%H -- scripts/fetch_model.sh | tail -1):scripts/fetch_model.sh" \
        >"${red_repo}/scripts/fetch_model.sh" 2>/dev/null \
        && chmod +x "${red_repo}/scripts/fetch_model.sh" \
        || { echo "SKIP: pre-hardening script not available in history"; return 0; }

    set +e
    PATH="${FIXTURES}:${PATH}" \
        FAKE_GIT_MODEL_TREE="${MODEL_TREE}" \
        MANIFOLD3_MODEL_ROOT="${MODEL_ROOT}" \
        "${red_repo}/scripts/fetch_model.sh" --version v1.0.0 \
        >"${TEST_ROOT}/red.stdout" 2>"${TEST_ROOT}/red.stderr"
    red_rc=$?
    set -e

    [ "${red_rc}" -eq 0 ] || fail "sandboxed original script did not complete"
    [ -e "${red_repo}/.local/models/model.onnx" ] \
        || fail "red test did not reproduce fixed repository output"
    [ ! -e "${MODEL_ROOT}/model.onnx" ] \
        || fail "original script unexpectedly honored MANIFOLD3_MODEL_ROOT"
    echo "PASS: red test reproduces original fixed-root defect only in sandbox"
}

test_real_model_tree_untouched() {
    CASE_NAME="real model tree untouched"
    local before after
    before="$(snapshot_models)"
    run_script --version v1.0.0
    expect_rc 0
    after="$(snapshot_models)"
    [ "${before}" = "${after}" ] || fail "test modified the real .local/models tree"
    echo "PASS: real model tree unchanged by test run"
}

# --- Task 2: exact tagged commit + argument handling ---
test_version_required() {
    CASE_NAME="version required"
    run_script
    expect_rc 2
    expect_err_contains "--version is required"
    echo "PASS: missing --version exits 2"
}

test_mutable_version_rejected() {
    CASE_NAME="mutable version rejected"
    run_script --version main
    expect_rc 2
    expect_err_contains "immutable release tag"
    echo "PASS: mutable version rejected"
}

test_option_value_required() {
    CASE_NAME="option value required"
    run_script --version
    expect_rc 2
    expect_err_contains "--version requires a value"
    echo "PASS: option without value exits 2"
}

test_out_option_rejected() {
    CASE_NAME="out option rejected"
    run_script --out /tmp/x --version v1.0.0
    expect_rc 2
    echo "PASS: --out rejected as unknown option"
}

test_missing_tag_rejected() {
    CASE_NAME="missing tag rejected"
    FAKE_GIT_MISSING_TAG=v9.9.9 run_script --version v9.9.9
    expect_rc 1
    expect_err_contains "tag not found"
    echo "PASS: nonexistent tag exits 1"
}

test_transport_error_rejected() {
    CASE_NAME="transport error"
    FAKE_GIT_TRANSPORT_ERROR=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "unable to access HF repo"
    echo "PASS: transport error reported distinctly"
}

test_annotated_tag_uses_peeled() {
    CASE_NAME="annotated tag peeled"
    FAKE_GIT_ANNOTATED_TAG=1 FAKE_GIT_PEELED_OID=99 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "tag moved during fetch"
    echo "PASS: annotated tag resolves peeled OID"
}

# --- Task 3: LFS materialization ---
test_git_lfs_required() {
    CASE_NAME="git lfs required"
    FAKE_GIT_NO_LFS=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "git lfs is required"
    echo "PASS: missing git lfs exits 1"
}

test_lfs_pointer_rejected() {
    CASE_NAME="lfs pointer rejected"
    FAKE_GIT_LEAVE_POINTER=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "Git LFS pointer"
    echo "PASS: unexpanded LFS pointer rejected"
}

# --- Task 4: checksum manifest ---
test_missing_manifest() {
    CASE_NAME="missing manifest"
    FAKE_GIT_MISSING_MANIFEST=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "SHA256SUMS not found"
    echo "PASS: missing SHA256SUMS rejected"
}

test_extra_manifest_entry() {
    CASE_NAME="extra manifest entry"
    FAKE_GIT_EXTRA_MANIFEST_ENTRY=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "must contain exactly"
    echo "PASS: extra manifest entry rejected"
}

test_bad_onnx_hash() {
    CASE_NAME="bad onnx hash"
    FAKE_GIT_BAD_ONNX_HASH=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "checksum verification failed"
    echo "PASS: wrong ONNX hash rejected"
}

test_bad_yaml_hash() {
    CASE_NAME="bad yaml hash"
    FAKE_GIT_BAD_YAML_HASH=1 run_script --version v1.0.0
    expect_rc 1
    expect_err_contains "checksum verification failed"
    echo "PASS: wrong YAML hash rejected"
}

# --- Task 5: atomic install ---
test_happy_atomic_install() {
    CASE_NAME="happy atomic install"
    run_script --version v1.0.0
    expect_rc 0
    [ -d "${MODEL_ROOT}/releases/v1.0.0" ] || fail "release dir missing"
    [ -f "${MODEL_ROOT}/releases/v1.0.0/model.onnx" ] || fail "model.onnx missing"
    [ -f "${MODEL_ROOT}/releases/v1.0.0/model.yaml" ] || fail "model.yaml missing"
    [ -L "${MODEL_ROOT}/current" ] || fail "current not a symlink"
    [ "$(readlink "${MODEL_ROOT}/current")" = "releases/v1.0.0" ] || fail "current wrong target"
    (cd "${MODEL_ROOT}/releases/v1.0.0" && sha256sum --check --strict SHA256SUMS) \
        || fail "installed release checksums do not verify"
    echo "PASS: atomic install produces verified release + current link"
}

test_idempotent_install() {
    CASE_NAME="idempotent install"
    run_script --version v1.0.0
    expect_rc 0
    run_script --version v1.0.0
    expect_rc 0
    expect_out_contains "already installed and verified"
    echo "PASS: identical release reinstall is idempotent"
}

test_failed_install_preserves_known_good() {
    CASE_NAME="failed install preserves known good"
    MODEL_ROOT="${TEST_ROOT}/case-${CASE_NAME// /-}/models"
    mkdir -p "${MODEL_ROOT}/releases/v0.9.0"
    printf 'known-good\n' >"${MODEL_ROOT}/releases/v0.9.0/model.onnx"
    ln -s "releases/v0.9.0" "${MODEL_ROOT}/current"
    FAKE_GIT_BAD_ONNX_HASH=1 run_script --version v1.0.0
    expect_rc 1
    [ "$(readlink "${MODEL_ROOT}/current")" = "releases/v0.9.0" ] \
        || fail "failed fetch changed current"
    [ "$(cat "${MODEL_ROOT}/releases/v0.9.0/model.onnx")" = "known-good" ] \
        || fail "failed fetch modified the existing release"
    [ ! -e "${MODEL_ROOT}/releases/v1.0.0" ] \
        || fail "failed fetch left a partial release"
    echo "PASS: failed fetch preserves known-good release and current"
}

test_preserve_known_good_on_missing_yaml() {
    CASE_NAME="missing yaml preserves known good"
    MODEL_ROOT="${TEST_ROOT}/case-${CASE_NAME// /-}/models"
    mkdir -p "${MODEL_ROOT}/releases/v0.9.0"
    ln -s "releases/v0.9.0" "${MODEL_ROOT}/current"
    FAKE_GIT_MISSING_MANIFEST=1 run_script --version v1.0.0
    expect_rc 1
    [ "$(readlink "${MODEL_ROOT}/current")" = "releases/v0.9.0" ] \
        || fail "current changed on missing yaml"
    echo "PASS: missing yaml leaves current unchanged"
}

test_install_failure_before_commit_leaves_no_partial() {
    CASE_NAME="install failure before commit"
    FAKE_INSTALL_FAIL_DEST_BASENAME=model.yaml run_script --version v1.0.0
    expect_rc 1
    [ ! -e "${MODEL_ROOT}/releases/v1.0.0" ] \
        || fail "failed install left a release dir"
    echo "PASS: install failure leaves no partial release"
}

test_install_isolation_never_touches_real() {
    CASE_NAME="install isolation never touches real"
    local before after
    before="$(snapshot_models)"
    run_script --version v1.0.0
    expect_rc 0
    after="$(snapshot_models)"
    [ "${before}" = "${after}" ] || fail "install touched the real .local/models tree"
    echo "PASS: install isolation verified"
}

test_signal_during_commit_leaves_complete_state() {
    CASE_NAME="signal during commit"
    local models ready_file rc
    models="${TEST_ROOT}/case-${CASE_NAME// /-}/models"
    ready_file="${TEST_ROOT}/case-${CASE_NAME// /-}/ready"
    mkdir -p "$(dirname "${ready_file}")"
    rc="$(FAKE_MV_PAUSE_AFTER_DEST="${models}/current" \
        FAKE_MV_READY_FILE="${ready_file}" FAKE_MV_SLEEP_SECONDS=8 python3 - \
        "${SCRIPT}" "${MODEL_TREE}" "${FIXTURES}" "${models}" "${ready_file}" <<'PYEOF'
import os, signal, subprocess, sys, time
script, model_tree, fixtures, models, ready_file = sys.argv[1:]
env = os.environ.copy()
env["PATH"] = f"{fixtures}:{os.environ.get('PATH','')}"
env["FAKE_GIT_MODEL_TREE"] = model_tree
env["MANIFOLD3_MODEL_ROOT"] = models
env["FAKE_MV_PAUSE_AFTER_DEST"] = f"{models}/current"
env["FAKE_MV_READY_FILE"] = ready_file
env["FAKE_MV_SLEEP_SECONDS"] = "8"
with open(os.devnull, "w") as devnull:
    proc = subprocess.Popen(["bash", script, "--version", "v1.0.0"],
                            env=env, stdout=devnull, stderr=devnull,
                            start_new_session=True)
    for _ in range(200):
        if os.path.exists(ready_file):
            break
        time.sleep(0.1)
    else:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait()
        print("NO_READY")
        sys.exit(3)
    os.killpg(proc.pid, signal.SIGINT)
    print(proc.wait())
PYEOF
)"
    [ "${rc}" = "NO_READY" ] && fail "commit window never reached"
    [ "${rc}" -eq 0 ] || fail "expected exit 0 (signal ignored during commit), got ${rc}"
    [ -L "${models}/current" ] || fail "current symlink missing after commit"
    [ "$(readlink "${models}/current")" = "releases/v1.0.0" ] || fail "current wrong target after commit"
    [ -f "${models}/releases/v1.0.0/model.onnx" ] || fail "release missing after commit"
    echo "PASS: signal during commit leaves complete new state"
}

test_signal_before_commit_rolls_back() {
    CASE_NAME="signal before commit"
    local models ready_file rc
    models="${TEST_ROOT}/case-${CASE_NAME// /-}/models"
    ready_file="${TEST_ROOT}/case-${CASE_NAME// /-}/ready"
    mkdir -p "${models}/releases/v0.9.0"
    printf 'known-good\n' >"${models}/releases/v0.9.0/model.onnx"
    ln -s "releases/v0.9.0" "${models}/current"
    mkdir -p "$(dirname "${ready_file}")"
    rc="$(FAKE_INSTALL_PAUSE_DEST_BASENAME=model.onnx \
        FAKE_INSTALL_READY_FILE="${ready_file}" FAKE_INSTALL_SLEEP_SECONDS=8 python3 - \
        "${SCRIPT}" "${MODEL_TREE}" "${FIXTURES}" "${models}" "${ready_file}" <<'PYEOF'
import os, signal, subprocess, sys, time
script, model_tree, fixtures, models, ready_file = sys.argv[1:]
env = os.environ.copy()
env["PATH"] = f"{fixtures}:{os.environ.get('PATH','')}"
env["FAKE_GIT_MODEL_TREE"] = model_tree
env["MANIFOLD3_MODEL_ROOT"] = models
env["FAKE_INSTALL_PAUSE_DEST_BASENAME"] = "model.onnx"
env["FAKE_INSTALL_READY_FILE"] = ready_file
env["FAKE_INSTALL_SLEEP_SECONDS"] = "8"
with open(os.devnull, "w") as devnull:
    proc = subprocess.Popen(["bash", script, "--version", "v1.0.0"],
                            env=env, stdout=devnull, stderr=devnull,
                            start_new_session=True)
    for _ in range(200):
        if os.path.exists(ready_file):
            break
        time.sleep(0.1)
    else:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait()
        print("NO_READY")
        sys.exit(3)
    os.killpg(proc.pid, signal.SIGTERM)
    print(proc.wait())
PYEOF
)"
    [ "${rc}" = "NO_READY" ] && fail "install stage never reached"
    [ "${rc}" -eq 143 ] || fail "expected exit 143 (SIGTERM), got ${rc}"
    [ "$(readlink "${models}/current")" = "releases/v0.9.0" ] \
        || fail "current changed on SIGTERM before commit"
    [ "$(cat "${models}/releases/v0.9.0/model.onnx")" = "known-good" ] \
        || fail "known-good release modified on SIGTERM"
    [ ! -e "${models}/releases/v1.0.0" ] \
        || fail "partial release left after SIGTERM rollback"
    echo "PASS: SIGTERM before commit rolls back to old state"
}

test_concurrent_fetch_locked() {
    CASE_NAME="concurrent fetch locked"
    local models ready_file rc second_rc second_err
    models="${TEST_ROOT}/case-${CASE_NAME// /-}/models"
    ready_file="${TEST_ROOT}/case-${CASE_NAME// /-}/ready"
    mkdir -p "$(dirname "${ready_file}")"
    rc="$(FAKE_INSTALL_PAUSE_DEST_BASENAME=model.onnx \
        FAKE_INSTALL_READY_FILE="${ready_file}" FAKE_INSTALL_SLEEP_SECONDS=6 python3 - \
        "${SCRIPT}" "${MODEL_TREE}" "${FIXTURES}" "${models}" "${ready_file}" <<'PYEOF'
import os, subprocess, sys, time
script, model_tree, fixtures, models, ready_file = sys.argv[1:]
env = os.environ.copy()
env["PATH"] = f"{fixtures}:{os.environ.get('PATH','')}"
env["FAKE_GIT_MODEL_TREE"] = model_tree
env["MANIFOLD3_MODEL_ROOT"] = models
env["FAKE_INSTALL_PAUSE_DEST_BASENAME"] = "model.onnx"
env["FAKE_INSTALL_READY_FILE"] = ready_file
env["FAKE_INSTALL_SLEEP_SECONDS"] = "6"
with open(os.devnull, "w") as devnull:
    first = subprocess.Popen(["bash", script, "--version", "v1.0.0"],
                             env=env, stdout=devnull, stderr=devnull,
                             start_new_session=True)
    for _ in range(200):
        if os.path.exists(ready_file):
            break
        time.sleep(0.1)
    else:
        first.kill()
        print("NO_READY")
        sys.exit(3)
    second = subprocess.run(
        ["bash", script, "--version", "v1.0.0"],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
    print(f"{second.returncode}|{second.stderr.strip()}")
    first.wait()
PYEOF
)"
    [ "${rc}" = "NO_READY" ] && fail "first install never reached install stage"
    second_rc="${rc%|*}"
    second_err="${rc#*|}"
    [ "${second_rc}" -ne 0 ] || fail "second fetch unexpectedly succeeded"
    echo "${second_err}" | grep -q "another model fetch is active" \
        || fail "second fetch did not report lock contention"
    [ -L "${models}/current" ] || fail "first fetch did not complete current link"
    echo "PASS: concurrent fetch fails with lock contention"
}

test_sandbox_reproduces_original_fixed_root
test_real_model_tree_untouched
test_version_required
test_mutable_version_rejected
test_option_value_required
test_out_option_rejected
test_missing_tag_rejected
test_transport_error_rejected
test_annotated_tag_uses_peeled
test_git_lfs_required
test_lfs_pointer_rejected
test_missing_manifest
test_extra_manifest_entry
test_bad_onnx_hash
test_bad_yaml_hash
test_happy_atomic_install
test_idempotent_install
test_failed_install_preserves_known_good
test_preserve_known_good_on_missing_yaml
test_install_failure_before_commit_leaves_no_partial
test_install_isolation_never_touches_real
test_signal_during_commit_leaves_complete_state
test_signal_before_commit_rolls_back
test_concurrent_fetch_locked
echo "ALL fetch_model_script cases passed"
