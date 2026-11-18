#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
runtime_dir="$project_dir/runtime"
download_dir=$(mktemp -d)

target_arch=${1:-$(uname -m)}

case "$target_arch" in
    x86_64|amd64)
        onnx_arch=linux-x64
        mediapipe_platform=manylinux_2_28_x86_64
        mediapipe_hash=b72e6d61a79d1080d29a96ba95e3cfa3e43f6c433c0acc3bc9b3eb7ac0ba103a
        onnxruntime_hash=13ab8084954fa4a47c777880180b90810d6020f021441395712b48a75b74c68b
        ;;
    aarch64|arm64)
        onnx_arch=linux-arm64
        mediapipe_platform=manylinux_2_28_aarch64
        mediapipe_hash=b60afadf1a6c0bf9aae5ae2be5c66257e4e951ff0a123e86e3f6d24647854793
        onnxruntime_hash=648ffa64fbe027ae27139109410900cf776a030dec2dbbac51053318cc44c286
        ;;
    *)
        printf 'Unsupported architecture: %s\n' "$target_arch" >&2
        exit 1
        ;;
esac

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
    python_cmd=$(command -v python3 || true)
    if [[ -z "$python_cmd" ]]; then
        printf 'python3 is required to download MediaPipe\n' >&2
        exit 1
    fi

    wheel_url=$("$python_cmd" - "$mediapipe_platform" <<'PY'
import json
import sys
import urllib.request

with urllib.request.urlopen("https://pypi.org/pypi/mediapipe/1.0.1/json") as reply:
    files = json.load(reply)["urls"]
for item in files:
    if item["packagetype"] == "bdist_wheel" and item["filename"].endswith(sys.argv[1] + ".whl"):
        print(item["url"])
        break
else:
    sys.exit("No MediaPipe wheel for " + sys.argv[1])
PY
)
    curl --fail --location --silent --show-error \
        --output "$download_dir/${wheel_url##*/}" "$wheel_url"

    wheel=$(find "$download_dir" -name 'mediapipe-*.whl' -print -quit)
    "$python_cmd" - "$wheel" "$runtime_dir/libmediapipe.so" <<'PY'
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
    onnx_package="$download_dir/onnxruntime.nupkg"
    curl --fail --location --silent --show-error \
        --output "$onnx_package" \
        "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/$onnx_version/microsoft.ml.onnxruntime.$onnx_version.nupkg"
    python3 - "$onnx_package" "$onnx_arch" "$runtime_dir" <<'PY'
import pathlib
import sys
import zipfile

package, arch, runtime = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
with zipfile.ZipFile(package) as archive:
    (runtime / "libonnxruntime.so").write_bytes(
        archive.read(f"runtimes/{arch}/native/libonnxruntime.so"))
    for header in ("onnxruntime_c_api.h", "onnxruntime_ep_c_api.h"):
        text = archive.read(f"build/native/include/{header}").replace(b"\r\n", b"\n")
        (runtime / "include" / header).write_bytes(text)
PY
fi

if [[ ! -f "$runtime_dir/mobileone_s0_gaze.onnx" ]]; then
    curl --fail --location --silent --show-error \
        --output "$runtime_dir/mobileone_s0_gaze.onnx" \
        https://github.com/yakhyo/gaze-estimation/releases/download/weights/mobileone_s0_gaze.onnx
fi

verify_file "$mediapipe_hash" \
    "$runtime_dir/libmediapipe.so"
verify_file fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1 \
    "$runtime_dir/hand_landmarker.task"
verify_file 64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff \
    "$runtime_dir/face_landmarker.task"
verify_file "$onnxruntime_hash" \
    "$runtime_dir/libonnxruntime.so"
verify_file 71125e66180a991d65c9bdbad4aa20daaa1f7a48c7a5c0fa5f18f250ac839a02 \
    "$runtime_dir/include/onnxruntime_c_api.h"
verify_file b92778f50c36ecdf53d0344e0129a78b306afed653f81bba8a9597e3e6e9546f \
    "$runtime_dir/include/onnxruntime_ep_c_api.h"
verify_file 8b4fdc4e3da44733c9a82e7776b411e4a39f94e8e285aee0fc85a548a55f7d9f \
    "$runtime_dir/mobileone_s0_gaze.onnx"

printf 'Runtime ready in %s\n' "$runtime_dir"
