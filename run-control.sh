#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir=$(cd -- "$project_dir/.." && pwd)/build-singularity-gestures

if [[ ! -f "$project_dir/runtime/libmediapipe.so" ||
      ! -f "$project_dir/runtime/hand_landmarker.task" ||
      ! -f "$project_dir/runtime/face_landmarker.task" ||
      ! -f "$project_dir/runtime/libonnxruntime.so" ||
      ! -f "$project_dir/runtime/mobileone_s0_gaze.onnx" ||
      ! -f "$project_dir/runtime/include/onnxruntime_c_api.h" ||
      ! -f "$project_dir/runtime/include/onnxruntime_ep_c_api.h" ]]; then
    "$project_dir/scripts/bootstrap-runtime.sh"
fi

if [[ ! -d "$build_dir/meson-private" ]]; then
    meson setup "$build_dir" "$project_dir"
fi

meson compile -C "$build_dir"
exec "$build_dir/singularity-hand-control" "$@"
