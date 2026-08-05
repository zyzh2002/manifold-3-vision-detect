#!/usr/bin/env bash
# Build a fake HF model tree: model.onnx + model.yaml in a directory.
#
# Usage: make_fake_model.sh <dir>
#   <dir> must already exist. Populates model.onnx and model.yaml.
set -euo pipefail

cd "$1"
printf 'ONNXBYTES\0fake-model' > model.onnx
cat > model.yaml <<'YAML'
model_name: tree-crown-yolo11-seg
version: test
input: { name: "images", dtype: "float32", shape: [1, 3, 1280, 1280] }
outputs: { name: "output0", dtype: "float32", shape: [1, 43, 25600] }
classes: [ "crown" ]
target_trt: "8.5.2"
YAML
(
    cd "$1"
    sha256sum model.onnx model.yaml > SHA256SUMS
)