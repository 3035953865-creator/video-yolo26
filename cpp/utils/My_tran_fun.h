#ifndef MY_TRAN_FUN_H
#define MY_TRAN_FUN_H

#include <opencv2/opencv.hpp>
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>

void cv_frame_to_image_buffer(const cv::Mat& frame, cv::Mat& rgb_frame, image_buffer_t* img_buf);
int open_lcd();
void display_on_lcd(const cv::Mat& frame);
void close_lcd();   
int lcd_color_test(int hold_ms = 1500);
#endif // MY_TRAN_FUN_H
