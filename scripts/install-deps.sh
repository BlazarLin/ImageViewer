#!/usr/bin/env bash
# =============================================================================
# 创建时间：2026-11-21
# 功能：在当前 Ubuntu 主机安装 ImageViewer 的构建依赖。
# 目的：把 README/CI 中的 apt 清单收敛为一处可重复执行的操作，并按发行版
#       差异（24.04 起的 t64 包名）做兼容处理；缺少可选组件时给出提示而不失败。
# 用法：
#   ./scripts/install-deps.sh            # 安装必需 + 可选依赖
#   ./scripts/install-deps.sh --minimal  # 只安装必需依赖（无翻译/Svg/打包工具）
# =============================================================================
set -euo pipefail

MINIMAL=0
for arg in "$@"; do
    case "$arg" in
        --minimal) MINIMAL=1 ;;
        -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "未知参数: $arg" >&2; exit 1 ;;
    esac
done

if ! command -v apt-get > /dev/null 2>&1; then
    echo "仅支持 Debian/Ubuntu（未找到 apt-get）" >&2
    exit 1
fi

SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
    SUDO="sudo"
fi

# 必需：Qt5 基础开发包 + OpenCV + 构建工具。
REQUIRED=(build-essential cmake qtbase5-dev libopencv-dev)
# 可选：QtSvg（当前未使用，保留兼容）、LinguistTools（翻译编译）、
#       Qt Quick/OpenGL 与额外图片格式插件（TIFF/WebP/TGA 等）、
#       打包与冒烟测试工具。
OPTIONAL=(libqt5svg5-dev qttools5-dev qttools5-dev-tools
          libqt5opengl5-desktop-dev qt5-image-formats-plugins
          patchelf xvfb)

install_list() {
    if [[ $# -eq 0 ]]; then
        return 0
    fi
    $SUDO apt-get install -y --no-install-recommends "$@"
}

echo "==> 更新软件包索引"
$SUDO apt-get update

echo "==> 安装必需依赖：${REQUIRED[*]}"
install_list "${REQUIRED[@]}"

if [[ "$MINIMAL" -eq 1 ]]; then
    echo "==> 已跳过可选依赖"
    exit 0
fi

# 可选依赖逐个安装：个别包在旧/新发行版上可能缺失，缺失不应中断构建。
for pkg in "${OPTIONAL[@]}"; do
    if apt-cache show "$pkg" > /dev/null 2>&1; then
        echo "==> 安装可选依赖：$pkg"
        install_list "$pkg" || echo "警告：$pkg 安装失败，跳过" >&2
    else
        echo "跳过（仓库中不存在）：$pkg" >&2
    fi
done

echo "==> 依赖安装完成，可执行：./scripts/build.sh Release --test"
