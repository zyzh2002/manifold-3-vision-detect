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

if [ "${DO_BUILD}" = true ]; then
    source "${REPO_ROOT}/scripts/setup_env.sh"
    cmake --build "${REPO_ROOT}/build-cross" --target stream_demo -j"$(nproc)"
fi

scp "${SSH_OPTS[@]}" "${REPO_ROOT}/build-cross/src/app/stream_demo" "dji@${TARGET_IP}:${REMOTE_BIN}"
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "chmod +x ${REMOTE_BIN}"

# Start the demo, capture a few seconds of stream, then stop it.
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" \
  "pkill -f stream_demo 2>/dev/null; sleep 1; nohup ${REMOTE_BIN} --port=8080 >/tmp/stream_demo.log 2>&1 & sleep 4"

# Pull 1 MB of the stream and validate multipart + JPEG magic.
BYTES="$(ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" \
  \"curl -sN --max-time 5 http://127.0.0.1:8080/ | head -c 1048576 | base64 -w0\")"

echo "${BYTES}" | base64 -d > /tmp/stream_demo_capture.bin

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
ssh "${SSH_OPTS[@]}" "dji@${TARGET_IP}" "pkill -f stream_demo; sleep 1; echo stopped"
echo "MJPG smoke test passed"
