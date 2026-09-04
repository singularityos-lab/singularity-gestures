#include "lab/camera.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

namespace sg::lab {

namespace {

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

Camera::Camera() {
    gst_init(nullptr, nullptr);
}

Camera::~Camera() {
    stop();
}

bool Camera::start(const std::string &device, int width, int height) {
    stop();
    const std::string pipeline_description =
        "v4l2src device=" + device +
        " ! decodebin ! videoconvert ! videoflip method=horizontal-flip"
        " ! videoscale ! video/x-raw,format=RGB,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        ",framerate=30/1,pixel-aspect-ratio=1/1"
        " ! appsink name=gesture_sink max-buffers=1 drop=true sync=false";

    GError *parse_error = nullptr;
    pipeline_ = gst_parse_launch(pipeline_description.c_str(), &parse_error);
    if (!pipeline_) {
        error_ = parse_error ? parse_error->message : "Could not create camera pipeline";
        g_clear_error(&parse_error);
        return false;
    }

    auto *element = gst_bin_get_by_name(GST_BIN(pipeline_), "gesture_sink");
    sink_ = GST_APP_SINK(element);
    GstAppSinkCallbacks callbacks {};
    callbacks.new_sample = reinterpret_cast<GstFlowReturn (*)(GstAppSink *, gpointer)>(
        &Camera::on_sample);
    gst_app_sink_set_callbacks(sink_, &callbacks, this, nullptr);

    const auto state = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (state == GST_STATE_CHANGE_FAILURE) {
        error_ = "Camera could not enter the playing state";
        stop();
        return false;
    }
    return true;
}

void Camera::stop() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
    if (sink_) {
        gst_object_unref(sink_);
        sink_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    std::lock_guard lock(frame_mutex_);
    frame_.reset();
}

std::shared_ptr<const CameraFrame> Camera::latest() const {
    std::lock_guard lock(frame_mutex_);
    return frame_;
}

const std::string &Camera::error() const {
    return error_;
}

int Camera::on_sample(GstAppSink *sink, void *data) {
    auto *camera = static_cast<Camera *>(data);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }
    const int result = camera->copy_sample(sample);
    gst_sample_unref(sample);
    return result;
}

int Camera::copy_sample(GstSample *sample) {
    GstCaps *caps = gst_sample_get_caps(sample);
    GstVideoInfo info;
    if (!caps || !gst_video_info_from_caps(&info, caps)) {
        return GST_FLOW_ERROR;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        return GST_FLOW_ERROR;
    }

    const int width = static_cast<int>(GST_VIDEO_INFO_WIDTH(&info));
    const int height = static_cast<int>(GST_VIDEO_INFO_HEIGHT(&info));
    const int source_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
    const int target_stride = width * 3;
    auto next = std::make_shared<CameraFrame>();
    next->pixels.resize(static_cast<size_t>(target_stride) * height);
    next->width = width;
    next->height = height;
    next->stride = target_stride;
    next->timestamp_ms = monotonic_ms();
    next->sequence = ++sequence_;

    for (int row = 0; row < height; ++row) {
        std::memcpy(next->pixels.data() + static_cast<size_t>(row) * target_stride,
                    map.data + static_cast<size_t>(row) * source_stride,
                    target_stride);
    }
    gst_buffer_unmap(buffer, &map);

    std::lock_guard lock(frame_mutex_);
    frame_ = std::move(next);
    return GST_FLOW_OK;
}

} // namespace sg::lab
