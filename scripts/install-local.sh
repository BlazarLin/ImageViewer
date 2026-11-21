#!/usr/bin/env bash
# =============================================================================
# 创建时间：2026-11-21
# 功能：在本机完成 ImageViewer 的构建、DEB 打包、安装与快捷启动栏固定。
# 目的：把“构建 → 打包 → 安装 → 加入收藏夹（Dock）”收敛为一条可重复命令，
#       便于在开发机上验证发布产物，而不是只验证构建目录里的可执行文件。
# 用法：
#   ./scripts/install-local.sh                 # 构建(Release) + 打包 + 安装 + 固定到 Dock
#   ./scripts/install-local.sh --skip-build    # 复用已有 build/linux 产物
#   ./scripts/install-local.sh --no-pin        # 安装但不修改 GNOME 收藏夹
#   ./scripts/install-local.sh --uninstall     # 卸载 DEB 并从收藏夹移除
# 说明：安装/卸载需要 sudo；固定到 Dock 使用当前登录会话的 gsettings（仅 GNOME）。
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BUILD_DIR="build/linux"
OUTPUT_DIR="dist"
DESKTOP_ID="ImageViewer.desktop"
SKIP_BUILD=0
DO_PIN=1
DO_UNINSTALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) SKIP_BUILD=1 ;;
        --no-pin) DO_PIN=0 ;;
        --uninstall) DO_UNINSTALL=1 ;;
        -h|--help) sed -n '2,14p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "未知参数: $1" >&2; exit 1 ;;
    esac
    shift
done

SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then
    SUDO="sudo"
fi

pin_favorite() {
    if ! command -v gsettings > /dev/null 2>&1; then
        echo "未找到 gsettings，跳过快捷启动栏固定" >&2
        return 0
    fi
    if [[ "${XDG_CURRENT_DESKTOP:-}" != *GNOME* ]]; then
        echo "当前桌面不是 GNOME（XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-未设置}），跳过固定；" \
             "可手动把 $DESKTOP_ID 拖到启动栏" >&2
        return 0
    fi
    local current=""
    if ! current="$(gsettings get org.gnome.shell favorite-apps 2>/dev/null)"; then
        echo "读取 org.gnome.shell favorite-apps 失败，跳过固定" >&2
        return 0
    fi
    if [[ "$current" == *"'$DESKTOP_ID'"* ]]; then
        echo "已在快捷启动栏中：$DESKTOP_ID"
        return 0
    fi
    local updated=""
    case "$current" in
        "@as []"|"[]"|"") updated="['$DESKTOP_ID']" ;;
        *) updated="${current%]}, '$DESKTOP_ID']" ;;
    esac
    if gsettings set org.gnome.shell favorite-apps "$updated"; then
        echo "已固定到快捷启动栏：$DESKTOP_ID"
    else
        echo "固定失败，可手动在应用列表中右键选择“添加到收藏夹”" >&2
    fi
}

unpin_favorite() {
    if ! command -v gsettings > /dev/null 2>&1; then
        return 0
    fi
    local current=""
    current="$(gsettings get org.gnome.shell favorite-apps 2>/dev/null || true)"
    [[ -n "$current" ]] || return 0
    [[ "$current" == *"'$DESKTOP_ID'"* ]] || return 0
    # 用 python 精确解析 GVariant 数组，避免字符串替换破坏其他条目。
    local updated=""
    updated="$(python3 - "$current" "$DESKTOP_ID" <<'PY'
import ast
import sys

raw, target = sys.argv[1], sys.argv[2]
try:
    items = ast.literal_eval(raw)
except (ValueError, SyntaxError):
    items = []
items = [item for item in items if item != target]
print("[" + ", ".join("'%s'" % item for item in items) + "]")
PY
)"
    gsettings set org.gnome.shell favorite-apps "$updated"
    echo "已从快捷启动栏移除：$DESKTOP_ID"
}

if [[ "$DO_UNINSTALL" -eq 1 ]]; then
    unpin_favorite
    if dpkg -s imageviewer > /dev/null 2>&1; then
        $SUDO dpkg -r imageviewer
        echo "已卸载 imageviewer"
    else
        echo "未安装 imageviewer，无需卸载"
    fi
    exit 0
fi

if [[ "$SKIP_BUILD" -eq 0 ]]; then
    ./scripts/build.sh Release --test
else
    echo "==> 跳过构建，复用 $BUILD_DIR"
fi

echo "==> 打包 DEB"
./scripts/package-deb.sh -b "$BUILD_DIR" -o "$OUTPUT_DIR"
DEB="$(ls -1t "$OUTPUT_DIR"/ImageViewer-*-linux-x64.deb | head -1)"
echo "==> 安装：$DEB"
$SUDO dpkg -i "$DEB"
$SUDO apt-get install -f -y --no-install-recommends > /dev/null 2>&1 || true

if command -v update-desktop-database > /dev/null 2>&1; then
    $SUDO update-desktop-database -q /usr/share/applications || true
fi

if [[ "$DO_PIN" -eq 1 ]]; then
    pin_favorite
fi

echo "==> 完成：从应用列表或启动栏启动 ImageViewer（$(dpkg -s imageviewer | grep -m1 '^Version' | awk '{print $2}')）"
