#!/bin/bash
set -e

BIN="/get/rknn_yolo26_cam"
MODEL="/get/model/yolo26n-rv1126b.rknn"
LIB_DIR="/get/lib"
LOG_FILE="/tmp/rknn_yolo26_cam.log"

VIDEO_DIR="${1:-.}"

VIDEO_PATH=$(find "$VIDEO_DIR" -maxdepth 1 -type f -iname "*.mp4" | sort | head -n 1)

if [ -z "$VIDEO_PATH" ]; then
  echo "在目录 $VIDEO_DIR 没找到 mp4 文件"
  exit 1
fi

export LD_LIBRARY_PATH="${LIB_DIR}:${LD_LIBRARY_PATH}"
echo "使用视频: $VIDEO_PATH"

# fb0 直显模式下，先停止桌面合成器，避免覆盖显示
killall weston weston-keyboard weston-desktop-shell 2>/dev/null || true
sleep 1

if pidof weston >/dev/null 2>&1; then
  echo "错误: weston 仍在运行，fb0 画面会被覆盖。请先彻底停掉 weston。"
  exit 1
fi

# 解除可能的屏幕 blank
echo 0 >/sys/class/graphics/fb0/blank 2>/dev/null || true

echo "日志输出到: $LOG_FILE"
"$BIN" "$MODEL" "$VIDEO_PATH" >"$LOG_FILE" 2>&1
