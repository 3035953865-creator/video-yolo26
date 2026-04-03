#include "My_tran_fun.h"

#include <iostream>

using namespace std;

int lcd_fd = -1;
static fb_var_screeninfo vinfo;//屏幕可变信息结构体
static fb_fix_screeninfo finfo;//屏幕固定信息结构体
static unsigned char* lcd_virt_addr = NULL;

static uint32_t pack_fb_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t pixel = 0;
    if (vinfo.red.length > 0) {
        pixel |= ((static_cast<uint32_t>(r) >> (8 - vinfo.red.length)) << vinfo.red.offset);
    }
    if (vinfo.green.length > 0) {
        pixel |= ((static_cast<uint32_t>(g) >> (8 - vinfo.green.length)) << vinfo.green.offset);
    }
    if (vinfo.blue.length > 0) {
        pixel |= ((static_cast<uint32_t>(b) >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
    }
    if (vinfo.transp.length > 0) {
        uint32_t alpha = (1u << vinfo.transp.length) - 1u;
        pixel |= (alpha << vinfo.transp.offset);
    }
    return pixel;
}

static void write_fb_pixel(int x, int y, uint32_t pixel)
{
    const int bytes_per_pixel = vinfo.bits_per_pixel / 8;
    const int x_phy = x + static_cast<int>(vinfo.xoffset);
    const int y_phy = y + static_cast<int>(vinfo.yoffset);
    if (x_phy < 0 || y_phy < 0 ||
        x_phy >= static_cast<int>(vinfo.xres_virtual) ||
        y_phy >= static_cast<int>(vinfo.yres_virtual)) {
        return;
    }
    uint8_t* dst = lcd_virt_addr + y_phy * finfo.line_length + x_phy * bytes_per_pixel;

    switch (bytes_per_pixel) {
    case 2:
        *reinterpret_cast<uint16_t*>(dst) = static_cast<uint16_t>(pixel);
        break;
    case 3:
        dst[0] = static_cast<uint8_t>(pixel & 0xFF);
        dst[1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
        dst[2] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
        break;
    case 4:
        *reinterpret_cast<uint32_t*>(dst) = pixel;
        break;
    default:
        break;
    }
}

void cv_frame_to_image_buffer(const cv::Mat& frame, cv::Mat& rgb_frame, image_buffer_t* img_buf)
{
    cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB); // OpenCV默认是BGR格式，转换为RGB格式
    img_buf->width = rgb_frame.cols;
    img_buf->height = rgb_frame.rows;
    img_buf->width_stride = (int)(rgb_frame.step[0]);
    img_buf->format = IMAGE_FORMAT_RGB888;
    img_buf->virt_addr = rgb_frame.data;
    img_buf->size = (int)(rgb_frame.total() * rgb_frame.elemSize());    
}

int open_lcd()
{
    lcd_fd = open("/dev/fb0", O_RDWR);
    if (lcd_fd < 0) {
        perror("无法打开LCD设备");
        return -1;
    }
    if (ioctl(lcd_fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
        perror("获取LCD可变信息失败");
        close(lcd_fd);
        lcd_fd = -1;
        return -1;
    }
    if (ioctl(lcd_fd, FBIOGET_FSCREENINFO, &finfo) != 0) {
        perror("获取LCD固定信息失败");
        close(lcd_fd);
        lcd_fd = -1;
        return -1;
    }
    
    cout << "LCD分辨率: " << vinfo.xres << "x" << vinfo.yres << endl;
    cout << "LCD每像素字节数: " << vinfo.bits_per_pixel / 8 << endl;    
    cout << "LCD虚拟分辨率: " << vinfo.xres_virtual << "x" << vinfo.yres_virtual << endl;
    cout << "LCD偏移: xoffset=" << vinfo.xoffset << ", yoffset=" << vinfo.yoffset << endl;
    cout << "LCD行字节: " << finfo.line_length << ", 显存大小: " << finfo.smem_len << endl;

    //内存映射
    lcd_virt_addr = (unsigned char*)mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, lcd_fd, 0);
    if(lcd_virt_addr == MAP_FAILED)
    {
        cout << "显存映射失败！！！！！" << endl;
        close(lcd_fd);
        lcd_fd = -1;
        return -1;
    }

    int unblank = FB_BLANK_UNBLANK;
    if (ioctl(lcd_fd, FBIOBLANK, unblank) != 0) {
        perror("解除LCD blank失败");
    }

    fb_var_screeninfo pan_info = vinfo;
    pan_info.xoffset = 0;
    pan_info.yoffset = 0;
    if (ioctl(lcd_fd, FBIOPAN_DISPLAY, &pan_info) == 0) {
        vinfo.xoffset = pan_info.xoffset;
        vinfo.yoffset = pan_info.yoffset;
    }

    cout << "LCD打开成功" << endl;
    return 0;
}

void display_on_lcd(const cv::Mat& frame)
{
    if (lcd_fd < 0 || lcd_virt_addr == NULL || lcd_virt_addr == MAP_FAILED) {
        cout << "LCD未初始化，无法显示图像" << endl;
        return;
    }
    if (frame.empty()) {
        cout << "输入帧为空，跳过显示" << endl;
        return;
    }

    cv::Mat rgb_frame;
    if (frame.channels() == 3) {
        cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
    } else if (frame.channels() == 1) {
        cv::cvtColor(frame, rgb_frame, cv::COLOR_GRAY2RGB);
    } else {
        cout << "不支持的图像通道数: " << frame.channels() << endl;
        return;
    }

    cv::Mat resized;
    cv::resize(rgb_frame, resized, cv::Size(vinfo.xres, vinfo.yres), 0, 0, cv::INTER_LINEAR);

    for (int y = 0; y < static_cast<int>(vinfo.yres); ++y) {
        const uint8_t* row = resized.ptr<uint8_t>(y);
        for (int x = 0; x < static_cast<int>(vinfo.xres); ++x) {
            const uint8_t r = row[x * 3 + 0];
            const uint8_t g = row[x * 3 + 1];
            const uint8_t b = row[x * 3 + 2];
            write_fb_pixel(x, y, pack_fb_pixel(r, g, b));
        }
    }
    msync(lcd_virt_addr, finfo.smem_len, MS_SYNC);
}

void close_lcd()
{
    if (lcd_virt_addr != NULL && lcd_virt_addr != MAP_FAILED) {
        munmap(lcd_virt_addr, finfo.smem_len);
        lcd_virt_addr = NULL;
    }
    if (lcd_fd >= 0) {
        close(lcd_fd);
        lcd_fd = -1;
    }
}

int lcd_color_test(int hold_ms)
{
    if (lcd_fd < 0 || lcd_virt_addr == NULL || lcd_virt_addr == MAP_FAILED) {
        cout << "LCD未初始化，请先调用 open_lcd()" << endl;
        return -1;
    }

    const int screen_w = static_cast<int>(vinfo.xres);
    const int screen_h = static_cast<int>(vinfo.yres);
    const int bytes_per_pixel = vinfo.bits_per_pixel / 8;
    if (bytes_per_pixel != 2 && bytes_per_pixel != 3 && bytes_per_pixel != 4) {
        cout << "不支持的LCD像素格式: " << vinfo.bits_per_pixel << " bpp" << endl;
        return -1;
    }

    const uint8_t color_bars[8][3] = {
        {255, 255, 255}, // white
        {255, 255, 0},   // yellow
        {0, 255, 255},   // cyan
        {0, 255, 0},     // green
        {255, 0, 255},   // magenta
        {255, 0, 0},     // red
        {0, 0, 255},     // blue
        {0, 0, 0}        // black
    };

    const int top_h = screen_h * 2 / 3;
    const int bottom_h = screen_h - top_h;

    for (int y = 0; y < top_h; ++y) {
        for (int x = 0; x < screen_w; ++x) {
            int idx = x * 8 / screen_w;
            if (idx < 0) {
                idx = 0;
            }
            if (idx > 7) {
                idx = 7;
            }
            write_fb_pixel(
                x,
                y,
                pack_fb_pixel(color_bars[idx][0], color_bars[idx][1], color_bars[idx][2]));
        }
    }

    for (int y = 0; y < bottom_h; ++y) {
        const int actual_y = y + top_h;
        for (int x = 0; x < screen_w; ++x) {
            uint8_t gray = static_cast<uint8_t>((x * 255) / (screen_w > 1 ? (screen_w - 1) : 1));
            if (y < bottom_h / 2) {
                write_fb_pixel(x, actual_y, pack_fb_pixel(gray, gray, gray));
            } else {
                write_fb_pixel(x, actual_y, pack_fb_pixel(gray, 0, 255 - gray));
            }
        }
    }

    msync(lcd_virt_addr, finfo.smem_len, MS_SYNC);

    cout << "LCD彩条测试图已显示" << endl;
    if (hold_ms > 0) {
        usleep(static_cast<useconds_t>(hold_ms) * 1000);
    }
    return 0;
}
