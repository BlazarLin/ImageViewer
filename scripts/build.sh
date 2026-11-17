#!/usr/bin/env bash
# =============================================================================
# Linux(Ubuntu) 构建/测试脚本，与 scripts/build.ps1 对应。
# 依赖（Ubuntu 20.04+，Qt 5.15 可获得完整的边缘缩放/拖动体验）：
#   sudo apt install build-essential cmake qtbase5-dev libqt5svg5-dev \
#                    libqt5concurrent5 libqt5opengl5-desktop-dev libopencv-dev
# 用法：
#   ./scripts/build.sh              # Release 构建
#   ./scripts/build.sh Debug        # Debug 构建
#   ./scripts/build.sh Release --test   # 构建并运行核心测试
# =============================================================================
set -euo pipefail

CFG="${1:-Release}"
RUN_TESTS=false
if [[ "${2:-}" == "--test" ]]; then
    RUN_TESTS=true
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/linux"

echo "==> 配置 ($CFG) Qt=$(qmake --version 2>/dev/null | grep -o '5\.[0-9.]*' || true)"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$CFG" \
    -DBUILD_TESTING=ON

echo "==> 构建"
cmake --build "$BUILD_DIR" --config "$CFG" --parallel "$(nproc)"

if $RUN_TESTS; then
    echo "==> 运行核心测试"
    "$BUILD_DIR/ImageViewerCoreTests"
fi

echo "==> 完成：$BUILD_DIR/ImageViewer"
