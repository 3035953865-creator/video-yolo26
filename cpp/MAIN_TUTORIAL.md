# main.cc 学习指南（YOLO26 + RKNN + fb0）中文版

这份文档用于帮助你一步一步重写 `main()`，边写边理解流程。  
内容基于以下文件：
- `cpp/main.cc`
- `cpp/yolo26.h`
- `cpp/postprocess.h`
- `cpp/fb0_display.h`
- `cpp/utils/image_utils.h`
- `cpp/utils/image_drawing.h`

## 1. `main()` 整体在做什么

程序主流程可以概括为：

1. 读取命令行参数：`<model_path> <video_path>`
2. 初始化后处理标签与 RKNN 模型上下文
3. 用 OpenCV 打开视频
4. 初始化 `/dev/fb0` 显示设备
5. 循环处理每一帧：
   - BGR 转 RGB
   - 把 `cv::Mat` 映射为 `image_buffer_t`
   - 做 letterbox 缩放到模型输入尺寸
   - `rknn_inputs_set` -> `rknn_run` -> `rknn_outputs_get`
   - `post_process` 解码检测框/类别/置信度
   - 画框和文字
   - 计算并显示 FPS / NPU FPS
   - 将结果显示到 fb0
6. 释放全部资源

## 2. 重写顺序清单（照着写）

按这个顺序写，最稳：

1. 函数入口与参数检查
   - `int main(int argc, char **argv)`
   - 判断 `argc == 3`

2. 上下文与模块初始化
   - `rknn_app_context_t rknn_app_ctx; memset(...)`
   - `init_post_process()`
   - `init_yolo26_model(model_path, &rknn_app_ctx)`

3. 输入输出对象准备
   - `cv::VideoCapture cap(video_path)`
   - `Fb0Display fb0_display; fb0_display.init()`
   - `cv::Mat src_frame, rgb_frame`
   - `image_buffer_t src_image`
   - `object_detect_result_list od_results`

4. 帧循环
   - `while (cap.read(src_frame))`
   - 判断 `src_frame.empty()`
   - `cv::cvtColor(..., cv::COLOR_BGR2RGB)`
   - 用 `rgb_frame` 填充 `src_image` 各字段

5. 每帧推理临时变量
   - `image_buffer_t dst_img`
   - `letterbox_t letter_box`
   - `rknn_input inputs[rknn_app_ctx.io_num.n_input]`
   - `rknn_output outputs[rknn_app_ctx.io_num.n_output]`
   - 对它们 `memset`

6. 预处理与输入设置
   - 分配 `dst_img.virt_addr = malloc(dst_img.size)`
   - `convert_image_with_letterbox(&src_image, &dst_img, &letter_box, 114)`
   - 填充 `inputs[0]` 的 `index/type/fmt/size/buf`
   - 调用 `rknn_inputs_set(...)`
   - 输入设置成功后 `free(dst_img.virt_addr)`

7. 推理与输出解码
   - `rknn_run(...)`
   - 设置 `outputs[i].index` 与 `outputs[i].want_float = !is_quant`
   - `rknn_outputs_get(...)`
   - `post_process(...)`
   - `rknn_outputs_release(...)`

8. 绘制与显示
   - 遍历 `od_results.count`
   - `draw_rectangle(...)`
   - `draw_text(...)`
   - 画 FPS 文字
   - `fb0_display.present(result_mat)`

9. 退出清理
   - `cap.release()`
   - `fb0_display.deinit()`
   - `deinit_post_process()`
   - `release_yolo26_model(&rknn_app_ctx)`

## 3. API 速查（重点理解这些）

### 3.1 模型与 RKNN

1. `init_yolo26_model(const char* model_path, rknn_app_context_t* app_ctx)`
   - 作用：加载 `.rknn`，创建 `rknn_ctx`，查询输入输出张量属性，并保存模型宽高通道。
   - 声明在 `cpp/yolo26.h`，实现见 `cpp/rknpu2/yolo26.cc`。

2. `rknn_inputs_set(ctx, n_input, inputs)`
   - 作用：将输入张量喂给 NPU 运行时。
   - 当前工程输入格式是 `UINT8 + NHWC + RGB`。

3. `rknn_run(ctx, nullptr)`
   - 作用：执行一次推理。

4. `rknn_outputs_get(ctx, n_output, outputs, NULL)`
   - 作用：取出推理结果输出张量。
   - 注意：必须搭配 `rknn_outputs_release(...)` 释放。

5. `release_yolo26_model(&ctx)`
   - 作用：释放输入输出属性内存并销毁 RKNN 上下文。

### 3.2 预处理与后处理

1. `convert_image_with_letterbox(src, dst, &letter_box, bg_color)`
   - 作用：按比例缩放并填充，避免拉伸变形。
   - `letter_box` 记录了 `x_pad/y_pad/scale`，后处理映射回原图坐标要用到。

2. `post_process(&rknn_app_ctx, outputs, &letter_box, BOX_THRESH, NMS_THRESH, &od_results)`
   - 作用：把输出张量解码为 `[x1,y1,x2,y2,score,class]`，并做阈值过滤、坐标映射。

3. `init_post_process()` / `deinit_post_process()`
   - 作用：加载/释放类别标签文件（`coco_80_labels_list.txt`）。

4. `coco_cls_to_name(cls_id)`
   - 作用：类别 id 转类别名字符串。

### 3.3 绘制与显示

1. `draw_rectangle(image_buffer_t*, x, y, w, h, color, thickness)`
2. `draw_text(image_buffer_t*, text, x, y, color, fontsize)`
3. `Fb0Display::init("/dev/fb0")`
4. `Fb0Display::present(cv::Mat rgb_frame)`
5. `Fb0Display::deinit()`

### 3.4 OpenCV

1. `cv::VideoCapture cap(video_path)`
2. `cap.isOpened()`
3. `cap.read(src_frame)`
4. `cv::cvtColor(src_frame, rgb_frame, cv::COLOR_BGR2RGB)`
5. `cv::Mat(height, width, CV_8UC3, data, stride)`（用已有内存构造 Mat）

## 4. 最小可重写骨架（先跑通，再优化）

```cpp
int main(int argc, char** argv) {
  if (argc != 3) return -1;
  const char* model_path = argv[1];
  const char* video_path = argv[2];

  rknn_app_context_t app;
  memset(&app, 0, sizeof(app));

  init_post_process();
  if (init_yolo26_model(model_path, &app) != 0) {
    deinit_post_process();
    return -1;
  }

  cv::VideoCapture cap(video_path);
  if (!cap.isOpened()) {
    release_yolo26_model(&app);
    deinit_post_process();
    return -1;
  }

  Fb0Display fb;
  if (!fb.init()) {
    cap.release();
    release_yolo26_model(&app);
    deinit_post_process();
    return -1;
  }

  cv::Mat src, rgb;
  image_buffer_t src_img;
  object_detect_result_list od;
  memset(&src_img, 0, sizeof(src_img));

  while (cap.read(src)) {
    if (src.empty()) break;
    cv::cvtColor(src, rgb, cv::COLOR_BGR2RGB);

    src_img.width = rgb.cols;
    src_img.height = rgb.rows;
    src_img.width_stride = rgb.step[0];
    src_img.virt_addr = rgb.data;
    src_img.format = IMAGE_FORMAT_RGB888;
    src_img.size = rgb.total() * rgb.elemSize();

    image_buffer_t dst_img;
    letterbox_t lb;
    rknn_input inputs[app.io_num.n_input];
    rknn_output outputs[app.io_num.n_output];
    memset(&dst_img, 0, sizeof(dst_img));
    memset(&lb, 0, sizeof(lb));
    memset(inputs, 0, sizeof(inputs));
    memset(outputs, 0, sizeof(outputs));
    memset(&od, 0, sizeof(od));

    dst_img.width = app.model_width;
    dst_img.height = app.model_height;
    dst_img.format = IMAGE_FORMAT_RGB888;
    dst_img.size = get_image_size(&dst_img);
    dst_img.virt_addr = (unsigned char*)malloc(dst_img.size);
    if (!dst_img.virt_addr) break;

    if (convert_image_with_letterbox(&src_img, &dst_img, &lb, 114) < 0) {
      free(dst_img.virt_addr);
      break;
    }

    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = app.model_width * app.model_height * app.model_channel;
    inputs[0].buf = dst_img.virt_addr;

    if (rknn_inputs_set(app.rknn_ctx, app.io_num.n_input, inputs) < 0) {
      free(dst_img.virt_addr);
      break;
    }
    free(dst_img.virt_addr);

    if (rknn_run(app.rknn_ctx, nullptr) < 0) break;

    for (int i = 0; i < app.io_num.n_output; ++i) {
      outputs[i].index = i;
      outputs[i].want_float = !app.is_quant;
    }
    if (rknn_outputs_get(app.rknn_ctx, app.io_num.n_output, outputs, NULL) < 0) break;

    post_process(&app, outputs, &lb, BOX_THRESH, NMS_THRESH, &od);
    rknn_outputs_release(app.rknn_ctx, app.io_num.n_output, outputs);

    for (int i = 0; i < od.count; ++i) {
      int x1 = od.results[i].box.left;
      int y1 = od.results[i].box.top;
      int x2 = od.results[i].box.right;
      int y2 = od.results[i].box.bottom;
      draw_rectangle(&src_img, x1, y1, x2 - x1, y2 - y1, COLOR_YELLOW, 3);
    }

    cv::Mat result(src_img.height, src_img.width, CV_8UC3, src_img.virt_addr, src_img.width_stride);
    if (!fb.present(result)) break;
  }

  cap.release();
  fb.deinit();
  deinit_post_process();
  release_yolo26_model(&app);
  return 0;
}
```

## 5. 常见错误（高频踩坑）

1. `rknn_outputs_get` 后忘记 `rknn_outputs_release`，导致资源泄漏。
2. 忘了 BGR 转 RGB，导致推理结果异常。
3. 错误路径忘记 `free(dst_img.virt_addr)`。
4. 画图时图像格式不对，绘制接口需要 `IMAGE_FORMAT_RGB888`。
5. 目标板运行时没配置 `LD_LIBRARY_PATH`，找不到动态库。

## 6. 建议学习路线（一遍过）

1. 先完整手敲一次骨架，不做优化。
2. 先跑通一个视频，确认输入输出链路正常。
3. 再加 FPS 和 NPU 耗时统计。
4. 再加每个检测框的日志打印。
5. 最后重构重复的清理逻辑（减少重复代码）。

如果你愿意，下一步我可以继续给你一版“逐行中文注释的 `main.cc` 对照稿”，你可以一行行对着练。
