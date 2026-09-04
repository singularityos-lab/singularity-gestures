# Singularity Gestures

> [!IMPORTANT]
> Report bugs and request features in the
> [Singularity Desktop tracker](https://github.com/singularityos-lab/singularity-desktop/issues/new/choose).

Native hand and gaze tracking for the Singularity Desktop Environment. Camera
frames are processed locally and kept in memory. Only numeric calibration data
is saved.

This project ships a C library, the `singularity-hand-control` desktop service,
and the `singularity-gesture-lab` calibration client.

## Requirements

- Meson >= 1.2 and a C++20 compiler
- SDL2, OpenGL, GStreamer, Cairo, Pango and X11 for the calibration client
- GTK4, gtk4-layer-shell, Wayland and xkbcommon for desktop control

The tracking runtime is downloaded separately and kept outside Git. It uses
[MediaPipe](https://github.com/google-ai-edge/mediapipe),
[ONNX Runtime](https://github.com/microsoft/onnxruntime), and the
[gaze-estimation](https://github.com/yakhyo/gaze-estimation) MobileOne model.

## Build and test

```sh
./scripts/bootstrap-runtime.sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Run the calibration client:

```sh
./run.sh
```

Run the desktop controller inside Singularity Desktop:

```sh
./run-control.sh
```

## License

LGPL-2.1-only, see [LICENSE](LICENSE). Imported Wayland protocols retain their
original license notices.
