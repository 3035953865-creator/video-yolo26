#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "3rdparty/allocator/drm/drm_mode.h"
#include <opencv2/core.hpp>

class DrmDisplay {
public:
    DrmDisplay();
    ~DrmDisplay();

    bool init(const char *device_path = "/dev/dri/card0");
    bool present(const cv::Mat &rgb_frame);
    void deinit();

    int width() const;
    int height() const;

private:
    bool select_display_pipeline();
    bool create_scanout_buffer();
    void release_scanout_buffer();
    bool copy_frame_to_scanout(const cv::Mat &rgb_frame);

    int drm_fd_;
    uint32_t connector_id_;
    uint32_t encoder_id_;
    uint32_t crtc_id_;
    uint32_t fb_id_;
    uint32_t dumb_handle_;
    uint32_t dumb_pitch_;
    size_t dumb_size_;
    uint8_t *map_;
    bool initialized_;

    struct drm_mode_modeinfo selected_mode_;
    int width_;
    int height_;

    cv::Mat canvas_rgb_;
    cv::Mat converted_bgra_;
};

#endif
