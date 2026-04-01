#ifndef FB0_DISPLAY_H
#define FB0_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include <linux/fb.h>
#include <opencv2/core.hpp>

class Fb0Display {
public:
    Fb0Display();
    ~Fb0Display();

    bool init(const char *device_path = "/dev/fb0");
    bool present(const cv::Mat &rgb_frame);
    void deinit();

    int width() const;
    int height() const;
    int bits_per_pixel() const;

private:
    bool copy_frame_to_fb(const cv::Mat &rgb_frame);
    uint32_t pack_pixel(uint8_t r, uint8_t g, uint8_t b) const;

    int fb_fd_;
    uint8_t *fb_ptr_;
    size_t fb_size_;
    bool initialized_;

    struct fb_var_screeninfo vinfo_;
    struct fb_fix_screeninfo finfo_;

    cv::Mat canvas_rgb_;
};

#endif
