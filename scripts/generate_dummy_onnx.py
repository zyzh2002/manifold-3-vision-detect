#!/usr/bin/env python3
"""Generates a dummy ONNX model with YOLO11-seg output shapes.

The network is a single Conv that downsamples the 1280x1280 input to 160x160
feature maps and produces the three YOLO11-seg outputs with correct shapes but
deterministic garbage values. Used to validate the inference pipeline before a
real model is available.
"""
import argparse

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

K_NUM_SPECIES = 2
K_NUM_AGE_BINS = 5
K_NUM_CLASSES = K_NUM_SPECIES + K_NUM_AGE_BINS  # 7
K_NUM_MASK_COEFFS = 32
K_INPUT_SIZE = 1280
K_FEAT = 160  # 1280 / 8
K_ANCHORS = 160 * 160  # 33600 for one stride; keep single-stride for the dummy
K_PROTO = 32

K_CLS_CHANNELS = 4 + K_NUM_CLASSES + K_NUM_MASK_COEFFS  # 43


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="build/dummy_yolo11_seg.onnx")
    args = parser.parse_args()

    inp = helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, K_INPUT_SIZE, K_INPUT_SIZE])
    # YOLO11-seg emits (1, 4+nc+32, anchors) per stride; dummy uses one stride.
    out0 = helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, K_CLS_CHANNELS, K_ANCHORS])
    out1 = helper.make_tensor_value_info("output1", TensorProto.FLOAT, [1, K_NUM_MASK_COEFFS, K_ANCHORS])
    out2 = helper.make_tensor_value_info("output2", TensorProto.FLOAT, [1, K_PROTO, K_FEAT, K_FEAT])

    # One conv with stride 8 downsamples 1280x1280 to 160x160 and maps 3 -> K_CLS_CHANNELS.
    # Flatten to (1, C, 160*160), then slice into the three outputs.
    w0 = numpy_helper.from_array(
        np.random.RandomState(0).randn(K_CLS_CHANNELS, 3, 1, 1).astype(np.float32) * 0.01,
        "conv_w",
    )
    conv = helper.make_node("Conv", ["images", "conv_w"], ["feat4d"], kernel_shape=[1, 1], strides=[8, 8], pads=[0, 0, 0, 0])
    shape_flat = numpy_helper.from_array(np.array([1, K_CLS_CHANNELS, K_ANCHORS], dtype=np.int64), "shape_flat")
    resh_flat = helper.make_node("Reshape", ["feat4d", "shape_flat"], ["feat"])
    # Slice feature dims: [0:43] -> output0; [4+nc : 4+nc+32] -> mask coeffs; proto from [0:32] reshaped.
    # Opset 17 Slice takes starts/ends/axes as inputs, so they are initializers.
    sl0_starts = numpy_helper.from_array(np.array([0, 0, 0], dtype=np.int64), "sl0_starts")
    sl0_ends = numpy_helper.from_array(np.array([1, K_CLS_CHANNELS, K_ANCHORS], dtype=np.int64), "sl0_ends")
    sl0_axes = numpy_helper.from_array(np.array([0, 1, 2], dtype=np.int64), "sl0_axes")
    sl0 = helper.make_node("Slice", ["feat", "sl0_starts", "sl0_ends", "sl0_axes"], ["output0"])
    # Mask coeffs live in channels [4+nc, 4+nc+32)
    sl1_starts = numpy_helper.from_array(np.array([0, 4 + K_NUM_CLASSES, 0], dtype=np.int64), "sl1_starts")
    sl1_ends = numpy_helper.from_array(
        np.array([1, 4 + K_NUM_CLASSES + K_NUM_MASK_COEFFS, K_ANCHORS], dtype=np.int64), "sl1_ends"
    )
    sl1_axes = numpy_helper.from_array(np.array([0, 1, 2], dtype=np.int64), "sl1_axes")
    sl1 = helper.make_node("Slice", ["feat", "sl1_starts", "sl1_ends", "sl1_axes"], ["output1"])
    proto_starts = numpy_helper.from_array(np.array([0, 0], dtype=np.int64), "proto_starts")
    proto_ends = numpy_helper.from_array(np.array([1, 32], dtype=np.int64), "proto_ends")
    proto_axes = numpy_helper.from_array(np.array([0, 1], dtype=np.int64), "proto_axes")
    proto = helper.make_node("Slice", ["feat", "proto_starts", "proto_ends", "proto_axes"], ["proto_slice"])
    resh = helper.make_node("Reshape", ["proto_slice", "shape_proto"], ["output2"])
    shape_proto = numpy_helper.from_array(np.array([1, K_PROTO, K_FEAT, K_FEAT], dtype=np.int64), "shape_proto")

    graph = helper.make_graph(
        [conv, resh_flat, sl0, sl1, proto, resh],
        "dummy_yolo11_seg",
        [inp],
        [out0, out1, out2],
        initializer=[w0, shape_flat, sl0_starts, sl0_ends, sl0_axes, sl1_starts, sl1_ends, sl1_axes, proto_starts, proto_ends, proto_axes, shape_proto],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    onnx.checker.check_model(model)
    onnx.save(model, args.out)
    print(f"Wrote {args.out}")
    for info in (out0, out1, out2):
        print(f"  {info.name}: {[d.dim_value for d in info.type.tensor_type.shape.dim]}")


if __name__ == "__main__":
    main()
