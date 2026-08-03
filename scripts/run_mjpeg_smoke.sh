#!/usr/bin/env bash
# Target-side smoke test for the MJPEG stream demo.
# Usage: scripts/run_mjpeg_smoke.sh <manifold3-ip> [--no-build]
# Prereqs: drone + Pilot online (liveview stream active), Smart3DExplore stopped.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH_KEY="${REPO_ROOT}/config/manifold3_id_rsa"
REMOTE_DIR="~/vision-detect"
REMOTE_BIN="${REMOTE_DIR}/stream_demo"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o ConnectTimeout=10)

TARGET_IP="$1"
DO_BUILD=true
[ "${2:-}" = "--no-build" ] && DO_BUILD=false

# Stop any leftover demo on every exit path. The pkill runs in its OWN ssh
# session: sshd executes commands via sh -c, and any command whose text
# contains a plain "stream_demo" (e.g. the binary path or the log path)
# would be matched by the pattern and SIGTERMed before nohup could start.
cleanup() {
    ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "pkill -f '[s]tream_demo' 2>/dev/null || true" || true
}
trap cleanup EXIT INT TERM

if [ "${DO_BUILD}" = true ]; then
    source "${REPO_ROOT}/scripts/setup_env.sh"
    "${REPO_ROOT}/scripts/configure_cross_with_credentials.sh"
    cmake --build "${REPO_ROOT}/build-cross" --target stream_demo -j"$(nproc)"
fi

scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build-cross/src/app/stream_demo" "dji@${TARGET_IP}:${REMOTE_BIN}"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "chmod +x ${REMOTE_BIN}"

# Stop any previous demo instance in its own session, then start the demo in
# a separate session. Never combine the two: the start command's text embeds
# the plain binary/log names, which a pkill pattern would match and use to
# SIGTERM the executing shell itself.
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "pkill -f '[s]tream_demo' 2>/dev/null || true"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" \
  "sleep 1; nohup ${REMOTE_BIN} --port=8081 >/tmp/stream_demo.log 2>&1 & sleep 4"

# Pull 1 MB of the stream from the host over the same path the browser uses
# (the device has no curl on JetPack 5.1.3). head -c terminates the pipeline
# early; curl then exits non-zero on the write error, so swallow that
# expected pipeline failure. --noproxy '*' bypasses any host HTTP proxy env.
curl -sN --noproxy '*' --max-time 5 "http://${TARGET_IP}:8081/" | head -c 1048576 \
  > /tmp/stream_demo_capture.bin || true

grep -q -- "--frame" <(head -c 200 /tmp/stream_demo_capture.bin) || {
    echo "FAIL: multipart boundary not found"; exit 1; }
grep -q -- "Content-Type: image/jpeg" <(head -c 200 /tmp/stream_demo_capture.bin) || {
    echo "FAIL: JPEG content type missing"; exit 1; }

# First JPEG frame starts with FF D8; scan for the first SOI marker.
python3 - <<'EOF'
data = open("/tmp/stream_demo_capture.bin", "rb").read()
idx = data.find(b"\xff\xd8")
assert idx >= 0, "no JPEG SOI marker in stream"
# Find the matching EOI; crude: require at least 10k bytes after SOI.
assert len(data) - idx > 10240, "JPEG frame suspiciously small"
print("OK: JPEG frame starts at byte %d, payload %d bytes" % (idx, len(data) - idx))
EOF

ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "tail -5 /tmp/stream_demo.log"
# The EXIT trap stops the demo and reports completion.
echo "MJPG smoke test passed"
