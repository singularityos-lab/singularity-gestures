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

## Use of Generative AI

Maintainers may use generative AI tools as assistants while working on singularity-gestures. Non-trivial assisted commits disclose the tool, model, and scope of the work.

AI tools may assist with code comments, documentation, repetitive code, and issue triage. Maintainers make project decisions and review every assisted change before it is merged.

Use these trailers for non-trivial assisted commits:

```plain
Assisted-by: <tool>:<model-version>
AI-Scope: <what the tool generated and the prompt or a short prompt summary>
```

Single-line completions, renames, and formatting changes do not need trailers.

Coding agents must also follow [AGENTS.md](AGENTS.md) before changing files,
creating commits, or opening pull requests.
