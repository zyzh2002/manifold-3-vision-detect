# Model Artifact Fetch Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make model retrieval safe and reproducible: tests never touch real local models, only exact tagged HF commits are accepted, Git LFS objects are materialized, publisher checksums are verified, and failed or interrupted fetches cannot replace a known-good release.

**Architecture:** Each HF tag is an immutable release containing `model.onnx`, `model.yaml`, and `SHA256SUMS`. The consumer resolves the tag commit, clones and materializes LFS into temporary storage, verifies the exact commit and checksums, then atomically installs `.local/models/releases/<tag>/` and updates `.local/models/current`. The final Phase 5B tensor/semantic ABI remains explicitly unfrozen until a real ONNX is exported and compared.

**Tech Stack:** Bash, git over SSH, Git LFS, `sha256sum`, CTest shell tests with fake git fixtures, Hugging Face model repo `zyzh0/tree-crown-yolo11-seg`, producer repo `zyzh2002/tree-crown-yolo11-seg`.

## Global Constraints

- Work on onboard branch `fix/model-fetch-hardening`; direct commits to `main` require explicit user authorization.
- Work on producer branch `fix/model-release-checksums` in the training repository; never mix commits between repositories.
- Model binaries, TensorRT engines, credentials, and fetched releases remain git-ignored.
- The runtime `.engine` is built on Manifold 3 with TensorRT 8.5.2 and is never committed.
- The caller must provide an immutable version tag; `main`, `master`, `HEAD`, ref paths, and range syntax are rejected.
- Successful fetch proves: exact tag commit checked out, Git LFS object materialized, manifest file set valid, both hashes verified.
- Installation is all-or-nothing. Before the documented commit point, failures/signals restore the old state; after the commit point, signals leave the complete new release and `current`. Partial or unreferenced releases are forbidden.
- Tests may not read, write, delete, or migrate real `.local/models` content. They use `MANIFOLD3_MODEL_ROOT` below one owned temporary root.
- `.agents/docs/` remains English. The already-used Chinese handoff prompt may be relocated to `docs/` only with byte-for-byte content preservation.

## File Map

- `scripts/fetch_model.sh` — strict arguments, tag resolution, LFS, manifest validation, atomic installation.
- `tests/scripts/test_fetch_model.sh` — isolated test harness and all behavior cases.
- `tests/scripts/fixtures/git` — fake `ls-remote`, `lfs version`, `clone`, `lfs pull`, and `rev-parse`.
- `tests/scripts/fixtures/install` — fake passthrough for deterministic copy failure and signal pause tests.
- `tests/scripts/fixtures/mv` — fake passthrough with before/after destination hooks for atomic publish tests.
- `tests/scripts/fixtures/make_fake_model.sh` — valid three-file release fixture.
- `.agents/docs/specs/2026-08-04-tree-crown-training-design.md` — transport contract and correct current/future status.
- `docs/handoffs/tree-crown-training-agent-prompt.md`, `docs/README.md`, `AGENTS.md` — language-correct handoff location and references.
- Producer repo: `publish.py`, `docs/publishing.md`, and producer tests.

---

### Task 1: Isolate Tests Without Touching Real Models

**Files:**
- Modify: `scripts/fetch_model.sh:20-27`
- Modify: `tests/scripts/test_fetch_model.sh`

**Interfaces:**
- Consumes: optional `MANIFOLD3_MODEL_ROOT` environment variable.
- Produces: normal root `${REPO_ROOT}/.local/models`; test root supplied by the harness.

- [ ] **Step 1: Prepare the onboard branch**

```bash
git status --short --branch
git switch -c fix/model-fetch-hardening
```

Expected: clean short-lived branch from the reviewed HEAD. If it already exists or the tree is dirty, inspect and stop instead of resetting.

- [ ] **Step 2: Build one owned temporary test root and cleanup trap**

At test startup:

```bash
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "${TEST_ROOT}"' EXIT
MODEL_TREE="${TEST_ROOT}/model-source"
MODEL_ROOT="${TEST_ROOT}/installed-models"
mkdir -p "${MODEL_TREE}" "${MODEL_ROOT}"
bash "${MAKE_MODEL}" "${MODEL_TREE}"
```

Every case directory, stdout/stderr log, fake git log, and installed release must live below `TEST_ROOT`. Remove every later bare `mktemp -d`.

- [ ] **Step 3: Reproduce the original fixed-root defect only in a sandboxed repository copy**

Before changing the production script, copy it into a repository-shaped sandbox:

```bash
RED_REPO="${TEST_ROOT}/red-repo"
mkdir -p "${RED_REPO}/scripts"
cp "${SCRIPT}" "${RED_REPO}/scripts/fetch_model.sh"
chmod +x "${RED_REPO}/scripts/fetch_model.sh"

set +e
PATH="${FIXTURES}:${PATH}" \
    FAKE_GIT_MODEL_TREE="${MODEL_TREE}" \
    MANIFOLD3_MODEL_ROOT="${MODEL_ROOT}" \
    "${RED_REPO}/scripts/fetch_model.sh" \
    >"${TEST_ROOT}/red.stdout" 2>"${TEST_ROOT}/red.stderr"
red_rc=$?
set -e

[ "${red_rc}" -eq 0 ] || fail "sandboxed original script did not complete"
[ -e "${RED_REPO}/.local/models/model.onnx" ] \
    || fail "red test did not reproduce fixed repository output"
[ ! -e "${MODEL_ROOT}/model.onnx" ] \
    || fail "original script unexpectedly honored MANIFOLD3_MODEL_ROOT"
```

This is safe red evidence: only `RED_REPO/.local/models` is modified. Do not snapshot or execute against the real repository model path.

- [ ] **Step 4: Add model-root injection to the production script**

```bash
DEFAULT_MODEL_ROOT="${REPO_ROOT}/.local/models"
MODEL_ROOT="${MANIFOLD3_MODEL_ROOT:-${DEFAULT_MODEL_ROOT}}"
```

All installation paths in later tasks derive from `MODEL_ROOT`.

- [ ] **Step 5: Harden test match helpers**

Patterns beginning with `--` must not be interpreted as grep options:

```bash
expect_out_contains() {
    grep -q -- "$1" "${LAST_OUT}" \
        || fail "stdout missing '$1': $(tr '\n' ' ' <"${LAST_OUT}")"
}

expect_err_contains() {
    grep -q -- "$1" "${LAST_ERR}" \
        || fail "stderr missing '$1': $(tr '\n' ' ' <"${LAST_ERR}")"
}
```

- [ ] **Step 6: Prove two test runs leave an arbitrary real model tree unchanged**

Use a recursive content snapshot that supports absent, legacy, or versioned layouts:

```bash
snapshot_models() {
    if [ ! -d "${REPO_ROOT}/.local/models" ]; then
        printf 'ABSENT\n'
        return
    fi
    tar --sort=name --mtime='UTC 1970-01-01' \
        -cf - -C "${REPO_ROOT}/.local" models | sha256sum
}

before="$(snapshot_models)"
bash tests/scripts/test_fetch_model.sh
bash tests/scripts/test_fetch_model.sh
after="$(snapshot_models)"
[ "${before}" = "${after}" ]
```

Expected: both suites pass and before equals after. Do not require legacy files to be absent and do not migrate them in tests.

- [ ] **Step 7: Commit**

```bash
git add scripts/fetch_model.sh tests/scripts/test_fetch_model.sh
git commit -m "fix: isolate model fetch tests from local artifacts"
```

---

### Task 2: Require and Verify an Exact Tagged Commit

**Files:**
- Modify: `scripts/fetch_model.sh`
- Modify: `tests/scripts/fixtures/git`
- Modify: `tests/scripts/test_fetch_model.sh`

**Interfaces:**
- Consumes: required `--version <tag>` and optional `--repo <owner/name>`.
- Produces: a temporary checkout whose `HEAD` equals the tag commit resolved immediately before clone.

- [ ] **Step 1: Remove the old `--out` interface**

Delete `--out` from usage, parsing, and tests. There is one production destination: `MODEL_ROOT`, defaulting to `.local/models`. Tests override it only with `MANIFOLD3_MODEL_ROOT`. Passing `--out` must be treated as an unknown option with exit 2. No CLI/environment precedence remains to interpret.

- [ ] **Step 2: Add argument tests**

Required cases:

```bash
test_version_required() {
    CASE_NAME="version required"
    run_script
    expect_rc 2
    expect_err_contains "--version is required"
}

test_mutable_version_rejected() {
    CASE_NAME="mutable version rejected"
    run_script --version main
    expect_rc 2
    expect_err_contains "immutable release tag"
}

test_option_value_required() {
    CASE_NAME="option value required"
    run_script --version
    expect_rc 2
    expect_err_contains "--version requires a value"
}
```

Also reject `master`, `HEAD`, values containing `/`, and values containing `..`.

Add `test_out_option_rejected` and require exit 2.

- [ ] **Step 3: Implement strict parsing with usage exit code 2**

```bash
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

VERSION=""
```

Call `require_value "$@"` before reading `$2`. After parsing:

```bash
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
```

- [ ] **Step 4: Add tag-not-found, transport-error, and moved-tag tests**

Controls and expected outcomes:

```text
FAKE_GIT_MISSING_TAG=v9.9.9 -> ls-remote exit 2 -> "tag not found".
FAKE_GIT_TRANSPORT_ERROR=1 -> ls-remote exit 128 + diagnostic -> "unable to access HF repo".
FAKE_GIT_HEAD_OID differs from FAKE_GIT_TAG_OID -> checkout rejected as "tag moved during fetch".
FAKE_GIT_ANNOTATED_TAG=1 -> consumer uses peeled ^{} commit OID.
```

- [ ] **Step 5: Resolve the tag commit and preserve transport diagnostics**

```bash
HF_URL="git@hf.co:${REPO}"
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
```

- [ ] **Step 6: Clone and prove the checkout commit matches**

```bash
GIT_LFS_SKIP_SMUDGE=1 git clone -q --branch "${VERSION}" --depth 1 \
    "${HF_URL}" "${TMP_DIR}/repo"
ACTUAL_COMMIT="$(git -C "${TMP_DIR}/repo" rev-parse HEAD)"
if [ "${ACTUAL_COMMIT}" != "${EXPECTED_COMMIT}" ]; then
    echo "ERROR: tag moved during fetch: expected ${EXPECTED_COMMIT}, got ${ACTUAL_COMMIT}" >&2
    exit 1
fi
```

- [ ] **Step 7: Extend fake git exactly**

Support:

```text
git ls-remote --exit-code --tags <url> refs/tags/<tag> refs/tags/<tag>^{}
git clone -q --branch <tag> --depth 1 <url> <dir>
git -C <dir> rev-parse HEAD
```

Record the fake checkout OID in `<dir>/.fake-head` during clone. `rev-parse HEAD` prints it.

- [ ] **Step 8: Run and commit**

```bash
bash tests/scripts/test_fetch_model.sh
git add scripts/fetch_model.sh tests/scripts/fixtures/git tests/scripts/test_fetch_model.sh
git commit -m "fix: require exact tagged HF model commits"
```

---

### Task 3: Materialize Git LFS and Reject Pointer Files

**Files:**
- Modify: `scripts/fetch_model.sh`
- Modify: `tests/scripts/fixtures/git`
- Modify: `tests/scripts/test_fetch_model.sh`

**Interfaces:**
- Produces: nonempty binary `model.onnx`; a Git LFS pointer is never accepted.

- [ ] **Step 1: Add LFS failure tests**

```text
FAKE_GIT_NO_LFS=1 -> `git lfs version` fails -> script fails before clone.
FAKE_GIT_LFS_PULL_ERROR=1 -> pull fails -> script fails.
FAKE_GIT_LEAVE_POINTER=1 -> pull succeeds but pointer remains -> script fails.
normal -> pointer is replaced by fixture ONNX -> script continues.
```

- [ ] **Step 2: Require Git LFS and materialize only model.onnx**

```bash
if ! git lfs version >/dev/null 2>&1; then
    echo "ERROR: git lfs is required to fetch model.onnx" >&2
    exit 1
fi

if ! git -C "${TMP_DIR}/repo" lfs pull --include="model.onnx" --exclude=""; then
    echo "ERROR: git lfs pull failed for model.onnx" >&2
    exit 1
fi
```

- [ ] **Step 3: Reject pointer and empty content**

```bash
ONNX_SRC="${TMP_DIR}/repo/model.onnx"
if grep -q '^version https://git-lfs.github.com/spec/v1$' "${ONNX_SRC}"; then
    echo "ERROR: model.onnx is still a Git LFS pointer" >&2
    exit 1
fi
if [ ! -s "${ONNX_SRC}" ]; then
    echo "ERROR: model.onnx is empty" >&2
    exit 1
fi
```

- [ ] **Step 4: Extend fake git commands**

Support:

```text
git lfs version
git -C <dir> lfs pull --include=model.onnx --exclude=
```

Clone initially writes a pointer. LFS pull copies `${FAKE_GIT_MODEL_TREE}/model.onnx` unless the relevant control requests an error or retained pointer.

- [ ] **Step 5: Run and commit**

```bash
bash tests/scripts/test_fetch_model.sh
git add scripts/fetch_model.sh tests/scripts/fixtures/git tests/scripts/test_fetch_model.sh
git commit -m "fix: materialize HF model LFS objects"
```

---

### Task 4: Publish and Verify SHA256SUMS

**Files (onboard):**
- Modify: `scripts/fetch_model.sh`
- Modify: `tests/scripts/fixtures/git`
- Modify: `tests/scripts/fixtures/make_fake_model.sh`
- Modify: `tests/scripts/test_fetch_model.sh`

**Files (producer repository):**
- Modify: `publish.py`
- Modify: `docs/publishing.md`
- Modify/Create: producer tests covering manifest generation and publication file lists.

**Interfaces:**
- Consumes: exactly `model.onnx`, `model.yaml`, `SHA256SUMS` at one tag.
- Produces: verified three-file release.

- [ ] **Step 1: Prepare the external producer repository and branch**

```bash
TRAINING_REPO_DIR="${HOME}/coding/tree-crown-yolo11-seg"
if [ ! -d "${TRAINING_REPO_DIR}/.git" ]; then
    git clone git@github.com:zyzh2002/tree-crown-yolo11-seg.git "${TRAINING_REPO_DIR}"
fi
git -C "${TRAINING_REPO_DIR}" status --short --branch
test -z "$(git -C "${TRAINING_REPO_DIR}" status --porcelain)"
git -C "${TRAINING_REPO_DIR}" fetch origin
git -C "${TRAINING_REPO_DIR}" switch main
git -C "${TRAINING_REPO_DIR}" merge --ff-only origin/main
test "$(git -C "${TRAINING_REPO_DIR}" rev-parse HEAD)" = \
     "$(git -C "${TRAINING_REPO_DIR}" rev-parse origin/main)"
git -C "${TRAINING_REPO_DIR}" switch -c fix/model-release-checksums
```

If dirty, default branch is not `main`, fast-forward fails, or the branch exists with unknown work, stop and inspect; do not reset it.

- [ ] **Step 2: Add producer checksum functions**

```python
import hashlib


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _write_checksums(onnx: Path, model_yaml: Path, output: Path) -> None:
    output.write_text(
        f"{_sha256(onnx)}  {onnx.name}\n"
        f"{_sha256(model_yaml)}  {model_yaml.name}\n",
        encoding="ascii",
    )
```

Generate `SHA256SUMS` after final `model.yaml`. Both token and SSH paths must create one atomic HF commit containing all three files and then create the requested tag on exactly that commit.

For the token path, replace independent `upload_file` calls with one commit:

```python
from huggingface_hub import CommitOperationAdd, HfApi

api = HfApi(token=token)
commit = api.create_commit(
    repo_id=hf_repo,
    repo_type="model",
    commit_message=f"publish {tag}",
    operations=[
        CommitOperationAdd(path_in_repo="model.onnx", path_or_fileobj=str(onnx)),
        CommitOperationAdd(path_in_repo="model.yaml", path_or_fileobj=str(model_yaml)),
        CommitOperationAdd(path_in_repo="SHA256SUMS", path_or_fileobj=str(checksums)),
    ],
)
api.create_tag(
    repo_id=hf_repo,
    repo_type="model",
    tag=tag,
    revision=commit.oid,
)
```

If the tag already exists, fail rather than move it. The SSH path already creates a local commit and tag; extend its `git add` to all three files and keep `git push origin main --tags`.

- [ ] **Step 3: Add producer tests before implementation**

Tests must assert:

```text
manifest has exactly two lines in model.onnx/model.yaml order
digests are 64 lowercase hex characters
changing one byte causes sha256sum --check --strict to fail
token upload receives all three filenames
token path creates one commit and tags exactly commit.oid
existing token-path tag is rejected and never moved
SSH git add receives all three filenames
SSH tag resolves to the newly created three-file commit
```

- [ ] **Step 4: Run and commit producer work in the producer directory**

```bash
uv run --directory "${TRAINING_REPO_DIR}" pytest
uv run --directory "${TRAINING_REPO_DIR}" ruff check .
git -C "${TRAINING_REPO_DIR}" add publish.py docs/publishing.md tests
git -C "${TRAINING_REPO_DIR}" status --short
git -C "${TRAINING_REPO_DIR}" commit -m "fix: publish checksums with model releases"
```

- [ ] **Step 5: Create a valid onboard fixture manifest**

At the end of `make_fake_model.sh`:

```bash
(
    cd "${DEST}"
    sha256sum model.onnx model.yaml > SHA256SUMS
)
```

- [ ] **Step 6: Add consumer manifest cases and fake controls**

```text
FAKE_GIT_MISSING_MANIFEST=1: clone omits SHA256SUMS.
FAKE_GIT_EXTRA_MANIFEST_ENTRY=1: manifest includes unexpected.bin.
FAKE_GIT_TRAVERSAL_MANIFEST=1: manifest names ../model.onnx.
FAKE_GIT_BAD_ONNX_HASH=1: manifest ONNX digest does not match.
FAKE_GIT_BAD_YAML_HASH=1: manifest YAML digest does not match.
normal: exact two-file manifest verifies.
```

- [ ] **Step 7: Validate manifest names and syntax before verification**

```bash
MANIFEST_SRC="${TMP_DIR}/repo/SHA256SUMS"
[ -s "${MANIFEST_SRC}" ] || {
    echo "ERROR: SHA256SUMS not found in ${REPO}@${VERSION}" >&2
    exit 1
}

manifest_names="$(awk '{print $2}' "${MANIFEST_SRC}" | LC_ALL=C sort)"
expected_names="$(printf '%s\n' model.onnx model.yaml | LC_ALL=C sort)"
if [ "${manifest_names}" != "${expected_names}" ]; then
    echo "ERROR: SHA256SUMS must contain exactly model.onnx and model.yaml" >&2
    exit 1
fi

if ! awk 'NF == 2 && length($1) == 64 && $1 ~ /^[0-9a-f]+$/ && \
    ($2 == "model.onnx" || $2 == "model.yaml") { next } { exit 1 }' \
    "${MANIFEST_SRC}"; then
    echo "ERROR: malformed SHA256SUMS" >&2
    exit 1
fi
```

- [ ] **Step 8: Verify checksums**

```bash
if ! (cd "${TMP_DIR}/repo" && sha256sum --check --strict SHA256SUMS); then
    echo "ERROR: model release checksum verification failed" >&2
    exit 1
fi
```

- [ ] **Step 9: Run and commit onboard work**

```bash
bash tests/scripts/test_fetch_model.sh
git add scripts/fetch_model.sh tests/scripts
git commit -m "fix: verify HF model release checksums"
```

---

### Task 5: Install Releases Atomically

**Files:**
- Modify: `scripts/fetch_model.sh`
- Modify: `tests/scripts/fixtures/git`
- Create: `tests/scripts/fixtures/install`
- Create: `tests/scripts/fixtures/mv`
- Modify: `tests/scripts/test_fetch_model.sh`

**Interfaces:**
- Produces: `MODEL_ROOT/releases/<tag>/{model.onnx,model.yaml,SHA256SUMS}` and `MODEL_ROOT/current -> releases/<tag>`.
- Guarantees: while holding a single-writer lock, every run ends in either the old valid state or the new valid state. No partial or unreferenced release remains after a pre-commit failure.

- [ ] **Step 1: Add known-good preservation tests**

Create under the test-only `MODEL_ROOT`:

```bash
mkdir -p "${MODEL_ROOT}/releases/v0.9.0"
printf 'known-good\n' >"${MODEL_ROOT}/releases/v0.9.0/model.onnx"
ln -s "releases/v0.9.0" "${MODEL_ROOT}/current"
```

For checksum, missing YAML, LFS pointer, moved tag, copy failure, `INT`, and `TERM`, assert:

```bash
[ "$(readlink "${MODEL_ROOT}/current")" = "releases/v0.9.0" ]
[ "$(cat "${MODEL_ROOT}/releases/v0.9.0/model.onnx")" = "known-good" ]
[ ! -e "${MODEL_ROOT}/releases/v1.0.0" ]
```

- [ ] **Step 2: Install cleanup handlers before acquiring the single-writer lock**

```bash
LOCK_FILE="${MODEL_ROOT}.fetch.lock"
TMP_DIR=""
STAGING_DIR=""
CURRENT_TMP=""
RELEASE_DIR="${MODEL_ROOT}/releases/${VERSION}"
RELEASE_CREATED=0
CURRENT_COMMITTED=0

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

TMP_DIR="$(mktemp -d)"
```

The final order is: install traps -> create exactly one `/tmp` `TMP_DIR` -> perform tag lookup, clone, LFS, and checksums without touching `MODEL_ROOT` -> only after complete verification, create the model root parent, acquire the lock, and create staging. Task 5 relocates the earlier Task 2-4 fetch/verification blocks below this `TMP_DIR` creation. Do not initialize `TMP_DIR` anywhere else.

After remote validation succeeds, acquire the installation lock outside the model tree:

```bash
mkdir -p "$(dirname "${MODEL_ROOT}")"
if ! command -v flock >/dev/null 2>&1; then
    echo "ERROR: flock is required for atomic model installation" >&2
    exit 1
fi
exec 9>"${LOCK_FILE}"
if ! flock -n 9; then
    echo "ERROR: another model fetch is active: ${LOCK_FILE}" >&2
    exit 1
fi
mkdir -p "${MODEL_ROOT}"
```

`LOCK_FILE` is the sibling `${MODEL_ROOT}.fetch.lock`, not a child of `MODEL_ROOT`, so model-tree snapshots exclude it. It may persist as an empty coordination file; the kernel lock is released automatically on exit. Add tests for `mktemp` failure cleanup, missing `flock`, concurrent fetch, and tag-not-found with an absent `MODEL_ROOT` (the root must remain absent).

- [ ] **Step 3: Create temporary directories after the lock is protected**

```bash
mkdir -p "${MODEL_ROOT}/releases"
STAGING_DIR="$(mktemp -d "${MODEL_ROOT}/.staging.${VERSION}.XXXXXX")"
```

Before the commit point, `on_signal` exits immediately and cleanup rolls back a release created by this run.

- [ ] **Step 4: Add deterministic install and move fixtures**

Create executable `tests/scripts/fixtures/install` that delegates to `/usr/bin/install` by default and supports:

```text
FAKE_INSTALL_FAIL_DEST_BASENAME=model.yaml -> exit 1 before copying that destination.
FAKE_INSTALL_PAUSE_DEST_BASENAME=model.onnx -> touch $FAKE_INSTALL_READY_FILE, then sleep for $FAKE_INSTALL_SLEEP_SECONDS.
```

Signal tests launch the script with Python `subprocess.Popen(..., start_new_session=True)`, wait for `FAKE_INSTALL_READY_FILE`, send `SIGINT` or `SIGTERM` using `os.killpg(proc.pid, signal)`, wait with a bounded timeout, and require exit 130/143. This mirrors the repository's existing interruption test pattern in `tests/scripts/test_extend_sysroot_from_device.sh`.

Create executable `tests/scripts/fixtures/mv` that delegates to `/usr/bin/mv` and supports:

```text
FAKE_MV_FAIL_DEST=<exact destination>: exit 1 before the real move.
FAKE_MV_PAUSE_BEFORE_DEST=<exact destination>: touch $FAKE_MV_READY_FILE, then sleep before the real move.
FAKE_MV_PAUSE_AFTER_DEST=<exact destination>: run the real move, touch $FAKE_MV_READY_FILE, then sleep before returning.
```

Parse the destination as the final argument, preserving all options when delegating. These hooks deterministically cover the release move and `current` move windows.

- [ ] **Step 5: Copy and reverify inside staging**

```bash
install -m 0644 "${ONNX_SRC}" "${STAGING_DIR}/model.onnx"
install -m 0644 "${META_SRC}" "${STAGING_DIR}/model.yaml"
install -m 0644 "${MANIFEST_SRC}" "${STAGING_DIR}/SHA256SUMS"
(cd "${STAGING_DIR}" && sha256sum --check --strict SHA256SUMS)
```

- [ ] **Step 6: Enforce immutable installed tags under the lock**

```bash
RELEASE_DIR="${MODEL_ROOT}/releases/${VERSION}"
if [ -e "${RELEASE_DIR}" ]; then
    if (cd "${RELEASE_DIR}" && sha256sum --check --strict SHA256SUMS >/dev/null 2>&1) && \
       cmp -s "${RELEASE_DIR}/SHA256SUMS" "${STAGING_DIR}/SHA256SUMS"; then
        echo "Release ${VERSION} is already installed and verified"
    else
        echo "ERROR: installed immutable tag ${VERSION} differs from HF" >&2
        exit 1
    fi
else
    # Mark rollback ownership before mv; cleanup can safely remove a target that does not yet exist.
    RELEASE_CREATED=1
    mv "${STAGING_DIR}" "${RELEASE_DIR}"
    STAGING_DIR=""
fi
```

Because `RELEASE_CREATED=1` is set before `mv`, a signal before, during, or immediately after the move causes cleanup to remove `RELEASE_DIR` when `CURRENT_COMMITTED=0`. Under the single-writer lock, that directory cannot belong to another fetch.

- [ ] **Step 7: Define and protect the commit point**

```bash
CURRENT_TMP="${MODEL_ROOT}/.current.${VERSION}.$$"
ln -s "releases/${VERSION}" "${CURRENT_TMP}"

# Ignore signals only across the atomic symlink replacement and state flag.
# SIG_IGN is inherited by mv, so a process-group INT/TERM cannot kill the child
# after rename but before the parent records the commit.
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
```

The commit point is `CURRENT_COMMITTED=1`. A signal before this critical section restores the old state. INT/TERM received during the critical section is intentionally ignored by both parent and `mv`; the operation completes successfully with the complete new release and `current`. Signal handling is restored immediately afterward. No post-rename external validation command exists in the signal-sensitive window.

- [ ] **Step 8: Cover idempotence, concurrency, failure, and upgrade behavior**

Required cases:

```text
empty root -> install v1.0.0
identical v1.0.0 -> success without mutation
different remote v1.0.0 -> reject
v1.1.0 -> install and switch current
every pre-commit failure -> current unchanged
INT and TERM before commit -> signal-specific exit and old state
INT and TERM during commit -> signal ignored, exit 0, complete new state
second concurrent fetch -> lock error, no mutation
failure after release mv but before current commit -> created release rolled back
signal deferred during current commit -> complete new state, never partial
```

Use `FAKE_MV_PAUSE_AFTER_DEST="${MODEL_ROOT}/releases/v1.0.0"` to signal after release publication and prove rollback. Use `FAKE_MV_PAUSE_BEFORE_DEST="${MODEL_ROOT}/current"` to send process-group SIGINT/SIGTERM while `mv` is in the signal-ignored commit section; fake `mv` must finish, the script must exit 0, and the complete new state must remain. Use `FAKE_MV_PAUSE_AFTER_DEST="${MODEL_ROOT}/current"` to prove the same behavior after the atomic rename but before `mv` returns.

- [ ] **Step 9: Run and commit**

```bash
bash tests/scripts/test_fetch_model.sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --test-dir build-host --output-on-failure
git add scripts/fetch_model.sh tests/scripts
git commit -m "fix: install validated model releases atomically"
```

---

### Task 6: Correct Documentation and Current/Future Status

**Files:**
- Modify: `.agents/docs/specs/2026-08-04-tree-crown-training-design.md`
- Move byte-for-byte: `.agents/docs/handoffs/tree-crown-training.md` -> `docs/handoffs/tree-crown-training-agent-prompt.md`
- Modify: `docs/README.md`
- Modify: `AGENTS.md`

**Interfaces:**
- Separates the implemented artifact transport from the pending Phase 5B inference ABI.

- [ ] **Step 1: Correct present-tense misstatements**

Replace claims that `TensorRtEngine` reads `model.yaml` with:

```text
Phase 5B will define and implement the TensorRtEngine contract after a real exported ONNX is available. The current TensorRtEngine accepts only synthetic_engine_contract.h and does not parse model.yaml.
```

- [ ] **Step 2: Document actual transport metadata, not a frozen semantic ABI**

```yaml
schema_version: 1
model_name: tree-crown-yolo11-seg
version: v1.0.0
input:
  name: images
  dtype: float32
  shape: [1, 3, 1280, 1280]
outputs:
  - name: output0
    dtype: float32
    shape: [1, "channels", "anchors"]
classes:
  - tree-crown
train_commit: "<40-character git sha>"
target_trt: "8.5.2"
```

State that `outputs` is a list and that final species/age/mask semantics, layout, preprocessing, letterbox, and label domains remain Phase 5B work.

- [ ] **Step 3: Document release and local layouts**

```text
HF tag vX.Y.Z: model.onnx, model.yaml, SHA256SUMS
.local/models/releases/vX.Y.Z/
.local/models/current -> releases/vX.Y.Z
```

- [ ] **Step 4: Relocate the Chinese prompt without changing it**

```bash
mkdir -p docs/handoffs
git mv .agents/docs/handoffs/tree-crown-training.md \
    docs/handoffs/tree-crown-training-agent-prompt.md
```

Before editing links, verify content identity against the parent commit:

```bash
git show HEAD^:.agents/docs/handoffs/tree-crown-training.md | sha256sum
sha256sum docs/handoffs/tree-crown-training-agent-prompt.md
```

Expected: hashes match. Update only references in `AGENTS.md` and `docs/README.md`.

- [ ] **Step 5: Use only pinned command examples**

```bash
scripts/fetch_model.sh --version v1.0.0
ls -l .local/models/current/model.onnx .local/models/current/model.yaml
```

- [ ] **Step 6: Commit**

```bash
git add AGENTS.md docs .agents/docs/specs/2026-08-04-tree-crown-training-design.md
git add -u .agents/docs/handoffs
git commit -m "docs: correct the model artifact handoff contract"
```

---

### Task 7: Final Verification

**Files:**
- Verify only.

- [ ] **Step 1: Syntax-check all shell files**

```bash
bash -n scripts/fetch_model.sh
bash -n tests/scripts/test_fetch_model.sh
bash -n tests/scripts/fixtures/git
bash -n tests/scripts/fixtures/make_fake_model.sh
```

- [ ] **Step 2: Run the host suite twice around a real-model-tree snapshot**

```bash
snapshot_models() {
    if [ ! -d .local/models ]; then
        printf 'ABSENT\n'
        return
    fi
    tar --sort=name --mtime='UTC 1970-01-01' -cf - -C .local models | sha256sum
}

before="$(snapshot_models)"
cmake --preset host-debug
cmake --build --preset host-debug
ctest --test-dir build-host --output-on-failure
ctest --test-dir build-host --output-on-failure
after="$(snapshot_models)"
test "${before}" = "${after}"
```

Expected: both suites pass and local model content is unchanged.

- [ ] **Step 3: Check a real negative HF path**

```bash
scripts/fetch_model.sh --version v0.0.0-does-not-exist
```

Expected: nonzero `tag not found`; local model snapshot remains unchanged.

- [ ] **Step 4: Check the first real checksummed release when available**

```bash
scripts/fetch_model.sh --version v1.0.0
(cd .local/models/releases/v1.0.0 && sha256sum --check --strict SHA256SUMS)
test "$(readlink .local/models/current)" = "releases/v1.0.0"
```

- [ ] **Step 5: Inspect and review before push**

```bash
git status --short
git diff --check
git log --oneline --decorate origin/main..HEAD
```

Request code review for the complete remediation range. Resolve every Critical and Important finding before asking for push approval.
