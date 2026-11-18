#!/usr/bin/env bash
# =============================================================================
# 将 Linux 构建产物打包为 Ubuntu DEB 安装包（内置全部运行库）。
#
# 2026-11-18：不再依赖目标机器预装 Qt/OpenCV。打包时用 ldd 递归收集
# 主程序与 Qt 插件的全部动态依赖（Qt5/OpenCV/ICU/X、GL 栈等，glibc 与
# libstdc++ 等基线库除外），拷入 /opt/ImageViewer/lib，并用 patchelf 为
# 主程序、各共享库与插件设置 $ORIGIN 系 RPATH，附带 qt.conf 指向随包
# 插件/翻译目录。目标系统仅需 glibc >= 2.35（Ubuntu 22.04+）。
#
# 用法：
#   ./scripts/package-deb.sh -b build -v 2.5.1 -o dist
# 产物：dist/ImageViewer-<版本>-linux-x64.deb
# 安装布局：
#   /opt/ImageViewer/ImageViewer            主程序
#   /opt/ImageViewer/lib/*.so.*             内置运行库
#   /opt/ImageViewer/plugins/...            Qt 平台与图片格式插件
#   /opt/ImageViewer/translations/*.qm      翻译
#   /opt/ImageViewer/qt.conf                Qt 路径配置
#   /usr/bin/ImageViewer -> /opt/...        命令行入口
#   /usr/share/applications/ImageViewer.desktop
#   /usr/share/icons/hicolor/256x256/apps/ImageViewer.png
# =============================================================================
set -euo pipefail

BUILD_DIR=""
VERSION=""
OUTPUT_DIR="dist"
SMOKE_TEST=1

while getopts "b:v:o:j" opt; do
    case "$opt" in
        b) BUILD_DIR="$OPTARG" ;;
        v) VERSION="$OPTARG" ;;
        o) OUTPUT_DIR="$OPTARG" ;;
        j) SMOKE_TEST=0 ;;
        *) echo "用法: $0 -b <构建目录> -v <版本号> [-o <输出目录>] [-j 跳过冒烟测试]" >&2; exit 1 ;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$VERSION" ]]; then
    echo "用法: $0 -b <构建目录> -v <版本号> [-o <输出目录>] [-j]" >&2
    exit 1
fi

BINARY="$BUILD_DIR/ImageViewer"
if [[ ! -x "$BINARY" ]]; then
    echo "未找到构建产物: $BINARY" >&2
    exit 1
fi
for tool in patchelf ldd qmake; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "缺少工具: $tool（patchelf 需 apt install patchelf）" >&2
        exit 1
    fi
done
if [[ ! -f "resources/ImageViewer.png" ]]; then
    echo "未找到图标资源: resources/ImageViewer.png" >&2
    exit 1
fi

# ---- 基线库排除清单：这些库随系统提供（glibc 2.35+ / gcc-12 运行时），不打包 ----
is_base_lib() {
    local base="$1"
    [[ "$base" =~ ^(libc\.|libm\.|libdl\.|libpthread\.|librt\.|libresolv|libnss_|libnsl|ld-linux|libBrokenLocale|libthread_db|libanl|libutil|libcrypt|libgcc_s|libstdc\+\+) ]]
}

APP_DIR_NAME="ImageViewer"
LIB_DIR_NAME="lib"

# 递归收集 ELF 的动态依赖：ldd 输出中的 SONAME 与真实路径成对记录，
# 基线库以外的依赖全部拷入 lib/，再对每个新库继续展开。
# 参数: $1 = 待分析 ELF; $2 = 已拷贝集合(按真实路径名)的文件
declare -A COPIED_SONAMES
collect_dependencies() {
    local elf="$1"
    local soname real_path real_base
    while read -r soname real_path; do
        [[ -n "$soname" && -n "$real_path" ]] || continue
        [[ -n "${COPIED_SONAMES[$soname]:-}" ]] && continue
        if [[ ! -e "$real_path" ]]; then
            echo "缺失动态依赖: $soname ($real_path)" >&2
            exit 1
        fi
        real_base="$(basename "$(realpath "$real_path")")"
        if is_base_lib "$real_base"; then
            COPIED_SONAMES["$soname"]="base"
            continue
        fi
        COPIED_SONAMES["$soname"]="$real_base"
        cp -L "$real_path" "$STAGE_LIB_DIR/$real_base"
        chmod 644 "$STAGE_LIB_DIR/$real_base"
        collect_dependencies "$STAGE_LIB_DIR/$real_base"
    done < <(ldd "$elf" 2>/dev/null | awk '/=> \//{print $1" "$3} /^[[:space:]]*\//{print $1" "$1}')
}

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

STAGE_APP_DIR="$STAGE/opt/$APP_DIR_NAME"
STAGE_LIB_DIR="$STAGE_APP_DIR/$LIB_DIR_NAME"
mkdir -p "$STAGE/DEBIAN" "$STAGE_LIB_DIR" \
    "$STAGE_APP_DIR/translations" \
    "$STAGE/usr/bin" \
    "$STAGE/usr/share/applications" \
    "$STAGE/usr/share/icons/hicolor/256x256/apps"

# ---- 主程序与运行库 ----
install -m 755 "$BINARY" "$STAGE_APP_DIR/ImageViewer"
patchelf --set-rpath '$ORIGIN/lib' "$STAGE_APP_DIR/ImageViewer"
collect_dependencies "$STAGE_APP_DIR/ImageViewer"

# ---- Qt 插件：平台(xcb)与图片格式，随包提供并由 qt.conf 指向 ----
QT_PLUGINS_DIR="$(qmake -query QT_INSTALL_PLUGINS)"
for plugin_group in platforms imageformats; do
    mkdir -p "$STAGE_APP_DIR/plugins/$plugin_group"
    cp -L "$QT_PLUGINS_DIR/$plugin_group/"*.so* "$STAGE_APP_DIR/plugins/$plugin_group/" 2>/dev/null || true
    for plugin in "$STAGE_APP_DIR/plugins/$plugin_group/"*; do
        [[ -e "$plugin" ]] || continue
        patchelf --set-rpath '$ORIGIN/../lib' "$plugin"
        chmod 644 "$plugin"
        collect_dependencies "$plugin"
    done
done

cat > "$STAGE_APP_DIR/qt.conf" <<EOF
[Paths]
Prefix = .
Plugins = plugins
Translations = translations
EOF

if ls "$BUILD_DIR/translations/"*.qm > /dev/null 2>&1; then
    cp -f "$BUILD_DIR/translations/"*.qm "$STAGE_APP_DIR/translations/"
fi

# ---- 一致性校验：所有 ELF 的依赖必须可解析（不再有 not found）----
while IFS= read -r elf; do
    if ldd "$elf" 2>/dev/null | grep -q "not found"; then
        echo "依赖解析失败: $elf" >&2
        ldd "$elf" | grep "not found" >&2
        exit 1
    fi
done < <(find "$STAGE_APP_DIR" -type f \( -name "ImageViewer" -o -name "*.so*" \))

echo "内置运行库数量: $(ls "$STAGE_LIB_DIR" | wc -l)"

# ---- 冒烟测试：用虚拟显示启动打包后的主程序，进程存活即认为依赖完整 ----
if [[ "$SMOKE_TEST" -eq 1 ]]; then
    if command -v xvfb-run > /dev/null 2>&1; then
        set +e
        QT_QPA_PLATFORM_PLUGIN_PATH="$STAGE_APP_DIR/plugins/platforms" \
            timeout 10 xvfb-run -a "$STAGE_APP_DIR/ImageViewer" > /dev/null 2>&1
        SMOKE_EXIT=$?
        set -e
        # 124 = timeout 杀掉了仍在运行的进程，说明窗口正常启动；1 可能是无图像参数的退出码。
        if [[ $SMOKE_EXIT -ne 124 && $SMOKE_EXIT -ne 1 && $SMOKE_EXIT -ne 0 ]]; then
            echo "冒烟测试失败（退出码 $SMOKE_EXIT），内置依赖可能不完整" >&2
            exit 1
        fi
        echo "冒烟测试通过（退出码 $SMOKE_EXIT）"
    else
        echo "未安装 xvfb-run，跳过冒烟测试" >&2
    fi
fi

# ---- 桌面集成 ----
ln -s "/opt/$APP_DIR_NAME/ImageViewer" "$STAGE/usr/bin/ImageViewer"
cp -f resources/ImageViewer.png "$STAGE/usr/share/icons/hicolor/256x256/apps/ImageViewer.png"

cat > "$STAGE/usr/share/applications/ImageViewer.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=ImageViewer
GenericName=Image Viewer
Comment=Fast local image viewer with OpenCV preprocessing and ROI analysis
Exec=ImageViewer %F
Icon=ImageViewer
Terminal=false
Categories=Graphics;Viewer;
MimeType=image/png;image/jpeg;image/jpg;image/bmp;image/gif;image/tiff;image/webp;image/x-xpixmap;
StartupNotify=true
EOF

cat > "$STAGE/DEBIAN/control" <<EOF
Package: imageviewer
Version: $VERSION
Architecture: amd64
Maintainer: Blazar <blazarlin@gmail.com>
Depends: libc6 (>= 2.35), libgcc-s1, libstdc++6 (>= 12)
Section: graphics
Priority: optional
Homepage: https://github.com/BlazarLin/ImageViewer
Description: 面向日常看图与工业视觉调试的桌面图像查看器
 打开本地图片，快速缩放、翻图、查看像素值，并提供 OpenCV
 预处理、直方图与 ROI 分析。深色主题，支持中文路径与高 DPI。
 运行所需的 Qt 与 OpenCV 库已随包内置，无需另行安装；
 目标系统要求 Ubuntu 22.04 或更新（glibc >= 2.35）。
EOF

mkdir -p "$OUTPUT_DIR"
OUTPUT="$OUTPUT_DIR/ImageViewer-$VERSION-linux-x64.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$OUTPUT"
echo "已生成: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
