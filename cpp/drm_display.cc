#include "drm_display.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <vector>

#include "3rdparty/allocator/drm/drm.h"
#include <opencv2/imgproc.hpp>

namespace {

const uint32_t kConnectorConnected = 1;

}

DrmDisplay::DrmDisplay()
    : drm_fd_(-1),
      connector_id_(0),
      encoder_id_(0),
      crtc_id_(0),
      fb_id_(0),
      dumb_handle_(0),
      dumb_pitch_(0),
      dumb_size_(0),
      map_(NULL),
      initialized_(false),
      width_(0),
      height_(0) {}

DrmDisplay::~DrmDisplay()
{
    deinit();
}

bool DrmDisplay::init(const char *device_path)
{
    if (initialized_) {
        return true;
    }

    drm_fd_ = open(device_path, O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0) {
        std::cerr << "Failed to open DRM device " << device_path
                  << ": " << strerror(errno) << std::endl;
        return false;
    }

    if (!select_display_pipeline()) {
        deinit();
        return false;
    }

    if (!create_scanout_buffer()) {
        deinit();
        return false;
    }

    struct drm_mode_crtc set_crtc;
    memset(&set_crtc, 0, sizeof(set_crtc));
    set_crtc.crtc_id = crtc_id_;
    set_crtc.fb_id = fb_id_;
    set_crtc.set_connectors_ptr = reinterpret_cast<uint64_t>(&connector_id_);
    set_crtc.count_connectors = 1;
    set_crtc.mode = selected_mode_;
    set_crtc.mode_valid = 1;

    if (ioctl(drm_fd_, DRM_IOCTL_MODE_SETCRTC, &set_crtc) != 0) {
        std::cerr << "Failed to set CRTC: " << strerror(errno) << std::endl;
        deinit();
        return false;
    }

    initialized_ = true;
    std::cout << "DRM display initialized at " << width_ << "x" << height_ << std::endl;
    return true;
}

bool DrmDisplay::present(const cv::Mat &rgb_frame)
{
    if (!initialized_ || rgb_frame.empty()) {
        return false;
    }

    return copy_frame_to_scanout(rgb_frame);
}

void DrmDisplay::deinit()
{
    release_scanout_buffer();

    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }

    connector_id_ = 0;
    encoder_id_ = 0;
    crtc_id_ = 0;
    width_ = 0;
    height_ = 0;
    initialized_ = false;
}

int DrmDisplay::width() const
{
    return width_;
}

int DrmDisplay::height() const
{
    return height_;
}

bool DrmDisplay::select_display_pipeline()
{
    struct drm_mode_card_res resources;
    memset(&resources, 0, sizeof(resources));
    if (ioctl(drm_fd_, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0) {
        std::cerr << "Failed to query DRM resources: " << strerror(errno) << std::endl;
        return false;
    }

    std::vector<uint32_t> connector_ids(resources.count_connectors);
    std::vector<uint32_t> encoder_ids(resources.count_encoders);
    std::vector<uint32_t> crtc_ids(resources.count_crtcs);

    resources.connector_id_ptr = reinterpret_cast<uint64_t>(connector_ids.data());
    resources.encoder_id_ptr = reinterpret_cast<uint64_t>(encoder_ids.data());
    resources.crtc_id_ptr = reinterpret_cast<uint64_t>(crtc_ids.data());

    if (ioctl(drm_fd_, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0) {
        std::cerr << "Failed to load DRM resources: " << strerror(errno) << std::endl;
        return false;
    }

    for (size_t i = 0; i < connector_ids.size(); ++i) {
        struct drm_mode_get_connector connector;
        memset(&connector, 0, sizeof(connector));
        connector.connector_id = connector_ids[i];

        if (ioctl(drm_fd_, DRM_IOCTL_MODE_GETCONNECTOR, &connector) != 0) {
            continue;
        }

        std::vector<uint32_t> encoders(connector.count_encoders);
        std::vector<struct drm_mode_modeinfo> modes(connector.count_modes);

        connector.encoders_ptr = reinterpret_cast<uint64_t>(encoders.data());
        connector.modes_ptr = reinterpret_cast<uint64_t>(modes.data());

        if (ioctl(drm_fd_, DRM_IOCTL_MODE_GETCONNECTOR, &connector) != 0) {
            continue;
        }

        if (connector.connection != kConnectorConnected || connector.count_modes == 0) {
            continue;
        }

        uint32_t chosen_encoder = connector.encoder_id;
        if (chosen_encoder == 0 && !encoders.empty()) {
            chosen_encoder = encoders[0];
        }
        if (chosen_encoder == 0) {
            continue;
        }

        struct drm_mode_get_encoder encoder;
        memset(&encoder, 0, sizeof(encoder));
        encoder.encoder_id = chosen_encoder;
        if (ioctl(drm_fd_, DRM_IOCTL_MODE_GETENCODER, &encoder) != 0) {
            continue;
        }

        uint32_t chosen_crtc = encoder.crtc_id;
        if (chosen_crtc == 0) {
            for (size_t crtc_index = 0; crtc_index < crtc_ids.size(); ++crtc_index) {
                if (encoder.possible_crtcs & (1u << crtc_index)) {
                    chosen_crtc = crtc_ids[crtc_index];
                    break;
                }
            }
        }
        if (chosen_crtc == 0 && !crtc_ids.empty()) {
            chosen_crtc = crtc_ids[0];
        }
        if (chosen_crtc == 0) {
            continue;
        }

        connector_id_ = connector.connector_id;
        encoder_id_ = chosen_encoder;
        crtc_id_ = chosen_crtc;
        selected_mode_ = modes[0];
        width_ = selected_mode_.hdisplay;
        height_ = selected_mode_.vdisplay;
        return true;
    }

    std::cerr << "No connected DRM connector with a valid mode was found." << std::endl;
    return false;
}

bool DrmDisplay::create_scanout_buffer()
{
    struct drm_mode_create_dumb create_dumb;
    memset(&create_dumb, 0, sizeof(create_dumb));
    create_dumb.width = width_;
    create_dumb.height = height_;
    create_dumb.bpp = 32;

    if (ioctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) != 0) {
        std::cerr << "Failed to create dumb buffer: " << strerror(errno) << std::endl;
        return false;
    }

    dumb_handle_ = create_dumb.handle;
    dumb_pitch_ = create_dumb.pitch;
    dumb_size_ = create_dumb.size;

    struct drm_mode_fb_cmd add_fb;
    memset(&add_fb, 0, sizeof(add_fb));
    add_fb.width = width_;
    add_fb.height = height_;
    add_fb.pitch = dumb_pitch_;
    add_fb.bpp = 32;
    add_fb.depth = 24;
    add_fb.handle = dumb_handle_;

    if (ioctl(drm_fd_, DRM_IOCTL_MODE_ADDFB, &add_fb) != 0) {
        std::cerr << "Failed to add framebuffer: " << strerror(errno) << std::endl;
        release_scanout_buffer();
        return false;
    }
    fb_id_ = add_fb.fb_id;

    struct drm_mode_map_dumb map_dumb;
    memset(&map_dumb, 0, sizeof(map_dumb));
    map_dumb.handle = dumb_handle_;

    if (ioctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) != 0) {
        std::cerr << "Failed to map dumb buffer: " << strerror(errno) << std::endl;
        release_scanout_buffer();
        return false;
    }

    map_ = static_cast<uint8_t *>(mmap(0, dumb_size_, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd_, map_dumb.offset));
    if (map_ == MAP_FAILED) {
        map_ = NULL;
        std::cerr << "Failed to mmap dumb buffer: " << strerror(errno) << std::endl;
        release_scanout_buffer();
        return false;
    }

    memset(map_, 0, dumb_size_);
    return true;
}

void DrmDisplay::release_scanout_buffer()
{
    if (map_ != NULL) {
        munmap(map_, dumb_size_);
        map_ = NULL;
    }

    if (fb_id_ != 0 && drm_fd_ >= 0) {
        uint32_t fb_id = fb_id_;
        ioctl(drm_fd_, DRM_IOCTL_MODE_RMFB, &fb_id);
        fb_id_ = 0;
    }

    if (dumb_handle_ != 0 && drm_fd_ >= 0) {
        struct drm_mode_destroy_dumb destroy_dumb;
        memset(&destroy_dumb, 0, sizeof(destroy_dumb));
        destroy_dumb.handle = dumb_handle_;
        ioctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
        dumb_handle_ = 0;
    }

    dumb_pitch_ = 0;
    dumb_size_ = 0;
}

bool DrmDisplay::copy_frame_to_scanout(const cv::Mat &rgb_frame)
{
    if (rgb_frame.type() != CV_8UC3) {
        std::cerr << "DRM display expects RGB888 frames." << std::endl;
        return false;
    }

    canvas_rgb_.create(height_, width_, CV_8UC3);
    canvas_rgb_.setTo(cv::Scalar(0, 0, 0));

    const double scale_x = static_cast<double>(width_) / rgb_frame.cols;
    const double scale_y = static_cast<double>(height_) / rgb_frame.rows;
    const double scale = std::min(scale_x, scale_y);

    const int resized_width = std::max(1, static_cast<int>(rgb_frame.cols * scale));
    const int resized_height = std::max(1, static_cast<int>(rgb_frame.rows * scale));
    const int offset_x = (width_ - resized_width) / 2;
    const int offset_y = (height_ - resized_height) / 2;

    cv::Mat resized_frame;
    cv::resize(rgb_frame, resized_frame, cv::Size(resized_width, resized_height), 0, 0, cv::INTER_LINEAR);
    resized_frame.copyTo(canvas_rgb_(cv::Rect(offset_x, offset_y, resized_width, resized_height)));

    cv::cvtColor(canvas_rgb_, converted_bgra_, cv::COLOR_RGB2BGRA);

    const size_t bytes_per_line = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        memcpy(map_ + static_cast<size_t>(y) * dumb_pitch_, converted_bgra_.ptr(y), bytes_per_line);
    }

    return true;
}
