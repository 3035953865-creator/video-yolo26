#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "yolo26.h"
#include "image_utils.h"
#include <opencv2/opencv.hpp>

#include "My_tran_fun.h"//自己包装的封装函数

using namespace std;
using namespace cv;

int main(int argc, char* argv[])
{
    cout << "加LCD版本" << endl;
    if (system("pidof weston > /dev/null 2>&1") == 0)
    {
        cout << "警告: 检测到 weston 正在运行，fb0 直写可能被桌面覆盖，建议先停 weston 再运行。" << endl;
    }
    // 单独LCD测试模式：无参数或 --lcd-test 都可进入
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--lcd-test") == 0))
    {
        if (open_lcd() != 0)
        {
            cout << "打开LCD失败" << endl;
            return -1;
        }

        // 先显示纯色块，快速判断 RGB 通道是否正常
        cv::Mat red_img(240, 320, CV_8UC3, cv::Scalar(0, 0, 255));   // BGR: Red
        cv::Mat green_img(240, 320, CV_8UC3, cv::Scalar(0, 255, 0)); // BGR: Green
        cv::Mat blue_img(240, 320, CV_8UC3, cv::Scalar(255, 0, 0));  // BGR: Blue
        cv::Mat white_img(240, 320, CV_8UC3, cv::Scalar(255, 255, 255));
        cv::Mat black_img(240, 320, CV_8UC3, cv::Scalar(0, 0, 0));

        display_on_lcd(red_img);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        display_on_lcd(green_img);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        display_on_lcd(blue_img);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        display_on_lcd(white_img);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        display_on_lcd(black_img);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        int ret = lcd_color_test(3000);
        close_lcd();
        return ret;
    }

    //检查参数
    if(argc != 3)
    {
        cout << "使用说明: " << argv[0] << " <模型路径> <视频路径>" << endl;
        cout << "LCD测试: " << argv[0] << " 或 " << argv[0] << " --lcd-test" << endl;
        return -1;
    }

    const char* MODEL_PATH = argv[1];
    const char* VIDEO_PATH = argv[2];

    //加载模型
    rknn_app_context_t app_ctx;//模型应用上下文对象
    memset(&app_ctx, 0, sizeof(rknn_app_context_t));//初始化上下文对象（内存清零）

    init_post_process();
    if (init_yolo26_model(MODEL_PATH, &app_ctx) != 0)
    {
        cout << "加载模型失败: " << MODEL_PATH << endl;
        deinit_post_process();
        return -1;
    }

    //打开LCD
    if (open_lcd() != 0)
    {
        cout << "打开LCD失败" << endl;
        release_yolo26_model(&app_ctx);
        deinit_post_process();
        return -1;
    }
    lcd_color_test(1500); // 上电后先显示1.5秒彩条，验证LCD颜色通道是否正常
    
    //加载视频(opencv)
    VideoCapture cap(VIDEO_PATH);
    if(!cap.isOpened())   
    {
        cout << "无法打开视频文件: " << argv[2] << endl;
        release_yolo26_model(&app_ctx);
        deinit_post_process();
        return -1;
    }
    cout << "视频打开成功: " << VIDEO_PATH << endl;
    cout << "视频信息: " << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH))
         << "x" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))
         << " fps=" << cap.get(cv::CAP_PROP_FPS) << endl;

    Mat frame;
    Mat rgb_frame;
    image_buffer_t img_buf;
    memset(&img_buf, 0, sizeof(img_buf));
    object_detect_result_list od_results;
    memset(&od_results, 0, sizeof(od_results));
    int frame_id = 0;
    int empty_frame_count = 0;
    while(true)
    {
        //读取视频帧，这里进行了>>重载，直接将帧数据读入Mat对象
        cap >> frame;
        if(frame.empty()) //如果帧数据为空，说明视频结束了
        {
            empty_frame_count++;
            if (empty_frame_count <= 30)
            {
                if (empty_frame_count == 1)
                {
                    cout << "读取到空帧，等待视频流稳定..." << endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            cout << "连续空帧，退出播放" << endl;
            break;
        }
        empty_frame_count = 0;
        frame_id++;
        Mat display_frame = frame.clone();

        // 先显示原始帧，避免推理阻塞时LCD无画面
        display_on_lcd(display_frame);

        cv_frame_to_image_buffer(frame, rgb_frame, &img_buf); //将OpenCV的Mat对象转换为我们定义的image_buffer_t结构体

        //模型推理
        auto infer_start = std::chrono::steady_clock::now();
        if (inference_yolo26_model(&app_ctx, &img_buf, &od_results) != 0)
        {
            cout << "模型推理失败, 帧号: " << frame_id << endl;
            cv::putText(display_frame, "INFERENCE FAILED", cv::Point(20, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
            display_on_lcd(display_frame);
            continue;
        }
        auto infer_end = std::chrono::steady_clock::now();
        double infer_ms = std::chrono::duration<double, std::milli>(infer_end - infer_start).count();

        cout << "Frame " << frame_id << ", 检测目标数: " << od_results.count
             << ", 推理耗时: " << fixed << setprecision(1) << infer_ms << " ms" << endl;
        for (int i = 0; i < od_results.count; ++i)
        {
            const object_detect_result& det = od_results.results[i];
            const char* class_name = coco_cls_to_name(det.cls_id);
            int x1 = std::max(0, det.box.left);
            int y1 = std::max(0, det.box.top);
            int x2 = std::min(display_frame.cols - 1, det.box.right);
            int y2 = std::min(display_frame.rows - 1, det.box.bottom);
            if (x2 <= x1 || y2 <= y1)
            {
                continue;
            }

            cout << "  [" << i << "] "
                 << "class=" << (class_name ? class_name : "unknown")
                 << " id=" << det.cls_id
                 << " score=" << fixed << setprecision(3) << det.prop
                 << " box=("
                 << det.box.left << ","
                 << det.box.top << ","
                 << det.box.right << ","
                 << det.box.bottom << ")"
                 << endl;

            cv::rectangle(display_frame, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 255, 0), 2);
            std::ostringstream oss;
            oss << (class_name ? class_name : "unknown")
                << " " << std::fixed << std::setprecision(2) << det.prop;
            int text_y = std::max(20, y1 - 5);
            cv::putText(display_frame, oss.str(), cv::Point(x1, text_y), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        }

        //lcd显示
        {
            std::ostringstream info;
            info << "Frame: " << frame_id << "  Obj: " << od_results.count;
            cv::putText(display_frame, info.str(), cv::Point(20, 35), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                        cv::Scalar(255, 0, 0), 2, cv::LINE_AA);
        }
        display_on_lcd(display_frame);
    }

    cap.release();
    close_lcd();
    release_yolo26_model(&app_ctx);
    deinit_post_process();

    return 0;
}
