#include "fb0_display.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>

#include <opencv2/imgproc.hpp>

Fb0Display::Fb0Display()
    : fb_fd_(-1),
      fb_ptr_(NULL),
      fb_size_(0),
      initialized_(false)
{
    memset(&vinfo_, 0, sizeof(vinfo_));
    memset(&finfo_, 0, sizeof(finfo_));
}

Fb0Display::~Fb0Display()
{
    deinit();
}

bool Fb0Display::init(const char *device_path)
{
    if (initialized_) {
        return true;
    }

    fb_fd_ = open(device_path, O_RDWR);
    if (fb_fd_ < 0) {
        std::cerr << "Failed to open framebuffer " << device_path
                  << ": " << strerror(errno) << std::endl;
        return false;
    }

    if (ioctl(fb_fd_, FBIOGET_FSCREENINFO, &finfo_) != 0) {
        std::cerr << "Failed to query fixed framebuffer info: "
                  << strerror(errno) << std::endl;
        deinit();
        return false;
    }

    if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &vinfo_) != 0) {
        std::cerr << "Failed to query variable framebuffer info: "
                  << strerror(errno) << std::endl;
        deinit();
        return false;
    }

    fb_size_ = static_cast<size_t>(finfo_.line_length) * vinfo_.yres_virtual;
    fb_ptr_ = static_cast<uint8_t *>(mmap(NULL, fb_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd_, 0));
    if (fb_ptr_ == MAP_FAILED) {
        fb_ptr_ = NULL;
        std::cerr << "Failed to mmap framebuffer: " << strerror(errno) << std::endl;
        deinit();
        return false;
    }

    memset(fb_ptr_, 0, fb_size_);
    initialized_ = true;

    std::cout << "fb0 display initialized at " << vinfo_.xres << "x" << vinfo_.yres
              << ", " << vinfo_.bits_per_pixel << " bpp" << std::endl;
    return true;
}

bool Fb0Display::present(const cv::Mat &rgb_frame)
{
    if (!initialized_ || rgb_frame.empty()) {
        return false;
    }

    return copy_frame_to_fb(rgb_frame);
}

void Fb0Display::deinit()
{
    if (fb_ptr_ != NULL) {
        munmap(fb_ptr_, fb_size_);
        fb_ptr_ = NULL;
    }

    if (fb_fd_ >= 0) {
        close(fb_fd_);
        fb_fd_ = -1;
    }

    fb_size_ = 0;
    initialized_ = false;
    memset(&vinfo_, 0, sizeof(vinfo_));
    memset(&finfo_, 0, sizeof(finfo_));
}

int Fb0Display::width() const
{
    return static_cast<int>(vinfo_.xres);
}

int Fb0Display::height() const
{
    return static_cast<int>(vinfo_.yres);
}

int Fb0Display::bits_per_pixel() const
{
    return static_cast<int>(vinfo_.bits_per_pixel);
}

bool Fb0Display::copy_frame_to_fb(const cv::Mat &rgb_frame)
{
    if (rgb_frame.type() != CV_8UC3) {
        std::cerr << "fb0 display expects RGB888 frames." << std::endl;
        return false;
    }

    const int screen_width = width();
    const int screen_height = height();
    canvas_rgb_.create(screen_height, screen_width, CV_8UC3);
    canvas_rgb_.setTo(cv::Scalar(0, 0, 0));

    const double scale_x = static_cast<double>(screen_width) / rgb_frame.cols;
    const double scale_y = static_cast<double>(screen_height) / rgb_frame.rows;
    const double scale = std::min(scale_x, scale_y);

    const int resized_width = std::max(1, static_cast<int>(rgb_frame.cols * scale));
    const int resized_height = std::max(1, static_cast<int>(rgb_frame.rows * scale));
    const int offset_x = (screen_width - resized_width) / 2;
    const int offset_y = (screen_height - resized_height) / 2;

    cv::Mat resized_frame;
    cv::resize(rgb_frame, resized_frame, cv::Size(resized_width, resized_height), 0, 0, cv::INTER_LINEAR);
    resized_frame.copyTo(canvas_rgb_(cv::Rect(offset_x, offset_y, resized_width, resized_height)));

    const int bytes_per_pixel = vinfo_.bits_per_pixel / 8;
    if (bytes_per_pixel <= 0) {
        std::cerr << "Unsupported framebuffer bytes-per-pixel value: " << bytes_per_pixel << std::endl;
        return false;
    }

    for (int y = 0; y < screen_height; ++y) {
        const uint8_t *src_row = canvas_rgb_.ptr<uint8_t>(y);
        uint8_t *dst_row = fb_ptr_ + static_cast<size_t>(y) * finfo_.line_length;

        for (int x = 0; x < screen_width; ++x) {
            const uint8_t r = src_row[x * 3 + 0];
            const uint8_t g = src_row[x * 3 + 1];
            const uint8_t b = src_row[x * 3 + 2];
            const uint32_t packed = pack_pixel(r, g, b);

            switch (bytes_per_pixel) {
            case 2:
                reinterpret_cast<uint16_t *>(dst_row)[x] = static_cast<uint16_t>(packed);
                break;
            case 3:
                dst_row[x * 3 + 0] = static_cast<uint8_t>(packed & 0xFF);
                dst_row[x * 3 + 1] = static_cast<uint8_t>((packed >> 8) & 0xFF);
                dst_row[x * 3 + 2] = static_cast<uint8_t>((packed >> 16) & 0xFF);
                break;
            case 4:
                reinterpret_cast<uint32_t *>(dst_row)[x] = packed;
                break;
            default:
                std::cerr << "Unsupported framebuffer pixel format: "
                          << vinfo_.bits_per_pixel << " bpp" << std::endl;
                return false;
            }
        }
    }

    return true;
}

uint32_t Fb0Display::pack_pixel(uint8_t r, uint8_t g, uint8_t b) const
{
    uint32_t pixel = 0;

    if (vinfo_.red.length > 0) {
        pixel |= ((static_cast<uint32_t>(r) >> (8 - vinfo_.red.length)) << vinfo_.red.offset);
    }
    if (vinfo_.green.length > 0) {
        pixel |= ((static_cast<uint32_t>(g) >> (8 - vinfo_.green.length)) << vinfo_.green.offset);
    }
    if (vinfo_.blue.length > 0) {
        pixel |= ((static_cast<uint32_t>(b) >> (8 - vinfo_.blue.length)) << vinfo_.blue.offset);
    }
    if (vinfo_.transp.length > 0) {
        uint32_t alpha = (1u << vinfo_.transp.length) - 1u;
        pixel |= (alpha << vinfo_.transp.offset);
    }

    return pixel;
}
