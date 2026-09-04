#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
runtime_dir="$project_dir/runtime"
download_dir=$(mktemp -d)

cleanup() {
    rm -rf -- "$download_dir"
}
trap cleanup EXIT

verify_file() {
    local expected=$1
    local path=$2
    local actual
    actual=$(sha256sum "$path" | cut -d' ' -f1)
    if [[ "$actual" != "$expected" ]]; then
        printf 'Checksum mismatch: %s\n' "$path" >&2
        return 1
    fi
}

mkdir -p "$runtime_dir/include"
if [[ ! -f "$runtime_dir/libmediapipe.so" ]]; then
    /usr/bin/python3.12 -m venv "$download_dir/venv"
    "$download_dir/venv/bin/python" -m pip download \
        --disable-pip-version-check \
        --only-binary=:all: \
        --no-deps \
        --dest "$download_dir" \
        mediapipe==1.0.1

    wheel=$(find "$download_dir" -name 'mediapipe-*.whl' -print -quit)
    /usr/bin/python3.12 - "$wheel" "$runtime_dir/libmediapipe.so" <<'PY'
import pathlib
import sys
import zipfile

wheel = pathlib.Path(sys.argv[1])
target = pathlib.Path(sys.argv[2])
member = "mediapipe/tasks/c/libmediapipe.so"
with zipfile.ZipFile(wheel) as archive:
    target.write_bytes(archive.read(member))
target.chmod(0o755)
PY
fi

if [[ ! -f "$runtime_dir/hand_landmarker.task" ]]; then
    curl --fail --location --silent --show-error \
        --output "$runtime_dir/hand_landmarker.task" \
        https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task
fi

if [[ ! -f "$runtime_dir/face_landmarker.task" ]]; then
    curl --fail --location --silent --show-error \
        --output "$runtime_dir/face_landmarker.task" \
        https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task
fi

if [[ ! -f "$runtime_dir/libonnxruntime.so" ||
      ! -f "$runtime_dir/include/onnxruntime_c_api.h" ||
      ! -f "$runtime_dir/include/onnxruntime_ep_c_api.h" ]]; then
    onnx_version=1.23.2
    onnx_archive="$download_dir/onnxruntime.tgz"
    curl --fail --location --silent --show-error \
        --output "$onnx_archive" \
        "https://github.com/microsoft/onnxruntime/releases/download/v$onnx_version/onnxruntime-linux-x64-$onnx_version.tgz"
    tar -xzf "$onnx_archive" -C "$download_dir"
    onnx_dir="$download_dir/onnxruntime-linux-x64-$onnx_version"
    cp "$onnx_dir/lib/libonnxruntime.so.$onnx_version" \
        "$runtime_dir/libonnxruntime.so"
    cp "$onnx_dir/include/onnxruntime_c_api.h" \
        "$runtime_dir/include/onnxruntime_c_api.h"
    cp "$onnx_dir/include/onnxruntime_ep_c_api.h" \
        "$runtime_dir/include/onnxruntime_ep_c_api.h"
fi

if [[ ! -f "$runtime_dir/mobileone_s0_gaze.onnx" ]]; then
    curl --fail --location --silent --show-error \
        --output "$runtime_dir/mobileone_s0_gaze.onnx" \
        https://github.com/yakhyo/gaze-estimation/releases/download/weights/mobileone_s0_gaze.onnx
fi

verify_file b72e6d61a79d1080d29a96ba95e3cfa3e43f6c433c0acc3bc9b3eb7ac0ba103a \
    "$runtime_dir/libmediapipe.so"
verify_file fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1 \
    "$runtime_dir/hand_landmarker.task"
verify_file 64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff \
    "$runtime_dir/face_landmarker.task"
verify_file 13ab8084954fa4a47c777880180b90810d6020f021441395712b48a75b74c68b \
    "$runtime_dir/libonnxruntime.so"
verify_file 71125e66180a991d65c9bdbad4aa20daaa1f7a48c7a5c0fa5f18f250ac839a02 \
    "$runtime_dir/include/onnxruntime_c_api.h"
verify_file b92778f50c36ecdf53d0344e0129a78b306afed653f81bba8a9597e3e6e9546f \
    "$runtime_dir/include/onnxruntime_ep_c_api.h"
verify_file 8b4fdc4e3da44733c9a82e7776b411e4a39f94e8e285aee0fc85a548a55f7d9f \
    "$runtime_dir/mobileone_s0_gaze.onnx"

printf 'Runtime ready in %s\n' "$runtime_dir"
