project('singularity-gestures', ['c', 'cpp'],
  version: '0.1.0',
  meson_version: '>= 1.2.0',
  default_options: [
    'buildtype=debugoptimized',
    'cpp_std=c++20',
    'warning_level=3',
  ],
)

cc = meson.get_compiler('cpp')

sdl_dep = dependency('sdl2')
gl_dep = dependency('gl')
glu_dep = dependency('glu')
gst_dep = dependency('gstreamer-1.0')
gst_app_dep = dependency('gstreamer-app-1.0')
gst_video_dep = dependency('gstreamer-video-1.0')
cairo_dep = dependency('cairo')
pango_dep = dependency('pangocairo')
x11_dep = dependency('x11')
gtk_dep = dependency('gtk4')
gio_dep = dependency('gio-2.0')
layer_shell_dep = dependency('gtk4-layer-shell-0')
wayland_dep = dependency('wayland-client')
xkbcommon_dep = dependency('xkbcommon')
threads_dep = dependency('threads')
dl_dep = cc.find_library('dl')

inc = include_directories('include', 'src', 'runtime/include')
gesture_install_dir = get_option('prefix') / get_option('datadir') / 'singularity' / 'gestures'
runtime_args = [
  '-DGESTURE_SOURCE_DIR="' + meson.current_source_dir() + '"',
  '-DGESTURE_INSTALL_DIR="' + gesture_install_dir + '"',
]

core_sources = files(
  'src/core/face_backend.cpp',
  'src/core/gaze_engine.cpp',
  'src/core/gaze_model.cpp',
  'src/core/gesture_engine.cpp',
  'src/core/gesture_math.cpp',
  'src/core/mediapipe_backend.cpp',
)

gesture_core = library(
  'singularity-gesture',
  core_sources,
  include_directories: inc,
  dependencies: [dl_dep, threads_dep],
  version: meson.project_version(),
  install: true,
)

gesture_dep = declare_dependency(
  include_directories: inc,
  link_with: gesture_core,
)

executable(
  'singularity-gesture-lab',
  files(
    'src/lab/camera.cpp',
    'src/lab/lab.cpp',
    'src/lab/main.cpp',
    'src/lab/renderer.cpp',
  ),
  include_directories: inc,
  cpp_args: runtime_args,
  dependencies: [
    gesture_dep,
    sdl_dep,
    gl_dep,
    glu_dep,
    gst_dep,
    gst_app_dep,
    gst_video_dep,
    cairo_dep,
    pango_dep,
    x11_dep,
    threads_dep,
  ],
  install: true,
)

wayland_scanner = find_program('wayland-scanner')
virtual_pointer_xml = 'protocols/wlr-virtual-pointer-unstable-v1.xml'
virtual_pointer_c = custom_target(
  'wlr-virtual-pointer-unstable-v1-protocol.c',
  input: virtual_pointer_xml,
  output: 'wlr-virtual-pointer-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
virtual_pointer_h = custom_target(
  'wlr-virtual-pointer-unstable-v1-client-protocol.h',
  input: virtual_pointer_xml,
  output: 'wlr-virtual-pointer-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

virtual_keyboard_xml = 'protocols/virtual-keyboard-unstable-v1.xml'
virtual_keyboard_c = custom_target(
  'virtual-keyboard-unstable-v1-protocol.c',
  input: virtual_keyboard_xml,
  output: 'virtual-keyboard-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
virtual_keyboard_h = custom_target(
  'virtual-keyboard-unstable-v1-client-protocol.h',
  input: virtual_keyboard_xml,
  output: 'virtual-keyboard-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

desktop_gesture_xml = 'protocols/singularity-gesture-unstable-v1.xml'
desktop_gesture_c = custom_target(
  'singularity-gesture-unstable-v1-protocol.c',
  input: desktop_gesture_xml,
  output: 'singularity-gesture-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
desktop_gesture_h = custom_target(
  'singularity-gesture-unstable-v1-client-protocol.h',
  input: desktop_gesture_xml,
  output: 'singularity-gesture-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'singularity-hand-control',
  files(
    'src/control/clipboard_gate.cpp',
    'src/control/guide_gate.cpp',
    'src/control/hand_controller.cpp',
    'src/control/gesture_gate.cpp',
    'src/control/main.cpp',
    'src/control/overlay.cpp',
    'src/control/screenshot_gate.cpp',
    'src/control/lock_gate.cpp',
    'src/control/toy_controller.cpp',
    'src/control/virtual_pointer.cpp',
    'src/lab/camera.cpp',
  ) + [virtual_pointer_c, virtual_pointer_h,
       virtual_keyboard_c, virtual_keyboard_h,
       desktop_gesture_c, desktop_gesture_h],
  include_directories: inc,
  cpp_args: runtime_args,
  dependencies: [
    gesture_dep,
    layer_shell_dep,
    gtk_dep,
    gio_dep,
    wayland_dep,
    xkbcommon_dep,
    gst_dep,
    gst_app_dep,
    gst_video_dep,
    cairo_dep,
    pango_dep,
    threads_dep,
  ],
  install: true,
)

install_headers(
  'include/singularity/gesture.h',
  'include/singularity/gaze.h',
  subdir: 'singularity',
)

install_data(
  'runtime/libmediapipe.so',
  'runtime/hand_landmarker.task',
  'runtime/face_landmarker.task',
  'runtime/libonnxruntime.so',
  'runtime/mobileone_s0_gaze.onnx',
  install_dir: gesture_install_dir / 'runtime',
)

pkg = import('pkgconfig')
pkg.generate(
  gesture_core,
  name: 'Singularity Gestures',
  filebase: 'singularity-gestures',
  description: 'Hand and gaze tracking for native Singularity clients',
)

test_gesture = executable(
  'test-gesture',
  files('tests/test_gesture.cpp', 'src/core/gesture_math.cpp'),
  include_directories: inc,
  dependencies: [threads_dep],
  install: false,
)

test('gesture-math', test_gesture)

test_gesture_gate = executable(
  'test-gesture-gate',
  files('tests/test_gesture_gate.cpp',
        'src/control/gesture_gate.cpp'),
  include_directories: inc,
  install: false,
)

test('gesture-gate', test_gesture_gate)

test_screenshot_gate = executable(
  'test-screenshot-gate',
  files('tests/test_screenshot_gate.cpp',
        'src/control/screenshot_gate.cpp'),
  include_directories: inc,
  install: false,
)

test('screenshot-gate', test_screenshot_gate)

test_lock_gate = executable(
  'test-lock-gate',
  files('tests/test_lock_gate.cpp',
        'src/control/lock_gate.cpp'),
  include_directories: inc,
  install: false,
)

test('lock-gate', test_lock_gate)

test_guide_gate = executable(
  'test-guide-gate',
  files('tests/test_guide_gate.cpp',
        'src/control/guide_gate.cpp'),
  include_directories: inc,
  install: false,
)

test('guide-gate', test_guide_gate)

test_clipboard_gate = executable(
  'test-clipboard-gate',
  files('tests/test_clipboard_gate.cpp',
        'src/control/clipboard_gate.cpp'),
  include_directories: inc,
  install: false,
)

test('clipboard-gate', test_clipboard_gate)

test_toy_controller = executable(
  'test-toy-controller',
  files('tests/test_toy_controller.cpp',
        'src/control/toy_controller.cpp'),
  include_directories: inc,
  install: false,
)

test('toy-controller', test_toy_controller)

test_runtime_paths = executable(
  'test-runtime-paths',
  files('tests/test_runtime_paths.cpp'),
  include_directories: inc,
  cpp_args: runtime_args,
  install: false,
)

test('runtime-paths', test_runtime_paths)

test_gaze = executable(
  'test-gaze',
  files('tests/test_gaze.cpp'),
  include_directories: inc,
  dependencies: [gesture_dep],
  install: false,
)

test('gaze-calibration', test_gaze)

test_gaze_model = executable(
  'test-gaze-model',
  files('tests/test_gaze_model.cpp'),
  include_directories: inc,
  dependencies: [gesture_dep],
  install: false,
)

test(
  'gaze-model',
  test_gaze_model,
  args: [
    meson.current_source_dir() / 'runtime/libonnxruntime.so',
    meson.current_source_dir() / 'runtime/mobileone_s0_gaze.onnx',
  ],
)

test_backend = executable(
  'test-backend',
  files('tests/test_backend.cpp'),
  include_directories: inc,
  dependencies: [gesture_dep],
  install: false,
)

test(
  'mediapipe-backend',
  test_backend,
  args: [
    meson.current_source_dir() / 'runtime/libmediapipe.so',
    meson.current_source_dir() / 'runtime/hand_landmarker.task',
    meson.current_source_dir() / 'runtime/face_landmarker.task',
    meson.current_source_dir() / 'runtime/libonnxruntime.so',
    meson.current_source_dir() / 'runtime/mobileone_s0_gaze.onnx',
  ],
)
