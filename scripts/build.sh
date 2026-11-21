#!/usr/bin/env bash
# =============================================================================
# Linux(Ubuntu) 构建/测试脚本，与 scripts/build.ps1 对应。
# 依赖（Ubuntu 22.04+ / 24.04，Qt 5.15 可获得完整的边缘缩放/拖动体验）：
#   必需：build-essential cmake qtbase5-dev libopencv-dev
#   可选：libqt5svg5-dev（QtSvg）、qttools5-dev（lrelease 编译翻译）
#   一键安装：./scripts/install-deps.sh
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
    # 桌面测试用例会创建真实窗口；无显示服务（CI/SSH）时改用 offscreen 平台。
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        echo "    未检测到显示服务，使用 QT_QPA_PLATFORM=offscreen"
        QT_QPA_PLATFORM=offscreen "$BUILD_DIR/ImageViewerCoreTests"
    else
        "$BUILD_DIR/ImageViewerCoreTests"
    fi
fi

echo "==> 完成：$BUILD_DIR/ImageViewer"
echo "    启动：$BUILD_DIR/ImageViewer [图片路径]"
echo "    打包：./scripts/package-deb.sh -b $BUILD_DIR"
