#!/usr/bin/env bash
# =============================================================================
# 将 Linux 构建产物打包为 Ubuntu DEB 安装包。
# 依赖系统 Qt/OpenCV 运行库（见 control 的 Depends），目标 Ubuntu 22.04+。
# 用法：
#   ./scripts/package-deb.sh -b build -v 2.5.0 -o dist
# 产物：dist/ImageViewer-<版本>-ubuntu-amd64.deb
# 安装布局：
#   /opt/ImageViewer/ImageViewer            主程序
#   /opt/ImageViewer/translations/*.qm      翻译
#   /usr/bin/ImageViewer -> /opt/...        命令行入口
#   /usr/share/applications/ImageViewer.desktop
#   /usr/share/icons/hicolor/256x256/apps/ImageViewer.png
# =============================================================================
set -euo pipefail

BUILD_DIR=""
VERSION=""
OUTPUT_DIR="dist"

while getopts "b:v:o:" opt; do
    case "$opt" in
        b) BUILD_DIR="$OPTARG" ;;
        v) VERSION="$OPTARG" ;;
        o) OUTPUT_DIR="$OPTARG" ;;
        *) echo "用法: $0 -b <构建目录> -v <版本号> -o <输出目录>" >&2; exit 1 ;;
    esac
done

if [[ -z "$BUILD_DIR" || -z "$VERSION" ]]; then
    echo "用法: $0 -b <构建目录> -v <版本号> [-o <输出目录>]" >&2
    exit 1
fi

BINARY="$BUILD_DIR/ImageViewer"
if [[ ! -x "$BINARY" ]]; then
    echo "未找到构建产物: $BINARY" >&2
    exit 1
fi
if [[ ! -f "resources/ImageViewer.png" ]]; then
    echo "未找到图标资源: resources/ImageViewer.png" >&2
    exit 1
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/DEBIAN" \
    "$STAGE/opt/ImageViewer/translations" \
    "$STAGE/usr/bin" \
    "$STAGE/usr/share/applications" \
    "$STAGE/usr/share/icons/hicolor/256x256/apps"

install -m 755 "$BINARY" "$STAGE/opt/ImageViewer/ImageViewer"
if ls "$BUILD_DIR/translations/"*.qm > /dev/null 2>&1; then
    cp -f "$BUILD_DIR/translations/"*.qm "$STAGE/opt/ImageViewer/translations/"
fi
ln -s /opt/ImageViewer/ImageViewer "$STAGE/usr/bin/ImageViewer"
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
Depends: libqt5core5a, libqt5gui5, libqt5widgets5, libqt5network5, libqt5concurrent5, libqt5svg5, libqt5dbus5, libopencv-core4.5d | libopencv-core406, libopencv-imgproc4.5d | libopencv-imgproc406, libopencv-imgcodecs4.5d | libopencv-imgcodecs406
Section: graphics
Priority: optional
Homepage: https://github.com/BlazarLin/ImageViewer
Description: 面向日常看图与工业视觉调试的桌面图像查看器
 打开本地图片，快速缩放、翻图、查看像素值，并提供 OpenCV
 预处理、直方图与 ROI 分析。深色主题，支持中文路径与高 DPI。
EOF

mkdir -p "$OUTPUT_DIR"
OUTPUT="$OUTPUT_DIR/ImageViewer-$VERSION-ubuntu-amd64.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$OUTPUT"
echo "已生成: $OUTPUT"
