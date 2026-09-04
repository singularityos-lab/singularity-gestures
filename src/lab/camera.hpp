#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

typedef struct _GstElement GstElement;
typedef struct _GstAppSink GstAppSink;
typedef struct _GstSample GstSample;

namespace sg::lab {

struct CameraFrame {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    int stride = 0;
    int64_t timestamp_ms = 0;
    uint64_t sequence = 0;
};

class Camera {
public:
    Camera();
    ~Camera();

    Camera(const Camera &) = delete;
    Camera &operator=(const Camera &) = delete;

    bool start(const std::string &device, int width, int height);
    void stop();
    std::shared_ptr<const CameraFrame> latest() const;
    const std::string &error() const;

private:
    GstElement *pipeline_ = nullptr;
    GstAppSink *sink_ = nullptr;
    mutable std::mutex frame_mutex_;
    std::shared_ptr<CameraFrame> frame_;
    std::atomic<uint64_t> sequence_ {0};
    std::string error_;

    static int on_sample(GstAppSink *sink, void *data);
    int copy_sample(GstSample *sample);
};

} // namespace sg::lab
