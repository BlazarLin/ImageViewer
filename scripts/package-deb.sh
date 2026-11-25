#!/usr/bin/env bash
# =============================================================================
# 将 Linux 构建产物打包为 Ubuntu DEB 安装包（内置全部运行库）。
#
# 2026-11-18：不再依赖目标机预装 Qt/OpenCV。打包时用 ldd 递归收集
# 主程序与 Qt 插件的全部动态依赖（Qt5/OpenCV/ICU/X、GL 栈等，glibc 与
# libstdc++ 等基线库除外），拷入 /opt/ImageViewer/lib，并用 patchelf 为
# 主程序、各共享库与插件设置 $ORIGIN 系 RPATH，附带 qt.conf 指向随包
# 插件/翻译目录。目标系统仅需 glibc >= 2.35（Ubuntu 22.04+）。
# 2026-11-21：显示/显卡 ABI 库（libGL/libEGL/libdrm/libgbm/libvulkan 等）不内置，
# 改由 Depends 声明并从系统加载，避免与 NVIDIA 等专有驱动冲突；插件目录扩展为
# 平台、图片格式、输入法、GL 集成、图标引擎与样式；新增 postinst/postrm 刷新
# 图标缓存与桌面数据库，并输出同名 .sha256 校验文件。
#
# 用法：
#   ./scripts/package-deb.sh -b build/linux                 # 版本号自动读取 src/app/AppVersion.h
#   ./scripts/package-deb.sh -b build/linux -v 2.5.2 -o dist
# 产物：dist/ImageViewer-<版本>-linux-x64.deb 与同名 .sha256
# 安装布局：
#   /opt/ImageViewer/ImageViewer            主程序
#   /opt/ImageViewer/lib/*.so.*             内置运行库
#   /opt/ImageViewer/plugins/...            Qt 平台、图片格式、输入法、GL 集成插件
#   /opt/ImageViewer/translations/*.qm      翻译
#   /opt/ImageViewer/qt.conf                Qt 路径配置
#   /usr/bin/ImageViewer -> /opt/...        命令行入口
#   /usr/share/applications/ImageViewer.desktop
#   /usr/share/icons/hicolor/256x256/apps/ImageViewer.png
# =============================================================================
set -euo pipefail

# 屏蔽调用方环境里的库搜索路径：否则 LD_LIBRARY_PATH（例如 CUDA 注入的
# /usr/local/cuda/lib64）会把个人环境的库当作依赖收进安装包，也会使 ldd 校验
# 被非系统路径污染，无法暴露真实的缺失依赖。
unset LD_LIBRARY_PATH LD_PRELOAD LD_AUDIT

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

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
        *) echo "用法: $0 -b <构建目录> [-v <版本号>] [-o <输出目录>] [-j 跳过冒烟测试]" >&2; exit 1 ;;
    esac
done

# 未显式指定版本时从 AppVersion.h 读取，避免发布包版本与程序不一致。
if [[ -z "$VERSION" ]]; then
    VERSION="$(grep -oP 'QString\("\K[0-9]+\.[0-9]+\.[0-9]+(?="\))' src/app/AppVersion.h | head -1 || true)"
fi

if [[ -z "$BUILD_DIR" || -z "$VERSION" ]]; then
    echo "用法: $0 -b <构建目录> [-v <版本号>] [-o <输出目录>] [-j]" >&2
    echo "提示: 未指定 -v 且无法从 src/app/AppVersion.h 解析版本号" >&2
    exit 1
fi

BINARY="$ROOT_DIR/$BUILD_DIR/ImageViewer"
if [[ ! -x "$BINARY" ]]; then
    echo "未找到构建产物: $BINARY（先执行 ./scripts/build.sh Release）" >&2
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

# ---- 显示/显卡 ABI 排除清单：这些库必须与目标机的内核驱动和 Mesa/NVIDIA 实现匹配，
# 不能随包携带。若把构建机的 Mesa 打进包内，目标机使用 NVIDIA 专有驱动时会因为找不到
# 对应的 _nvidia 扩展而 GL 初始化失败；排除后由系统 ldconfig 解析，并在 Depends 中声明。
is_display_abi_lib() {
    local base="$1"
    [[ "$base" =~ ^(libGL\.|libGLX|libGLdispatch|libOpenGL|libEGL|libGLESv|libglapi|libdrm|libgbm|libvulkan|libnvidia|libwayland-egl) ]]
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
        if is_display_abi_lib "$real_base"; then
            COPIED_SONAMES["$soname"]="system"
            continue
        fi
        COPIED_SONAMES["$soname"]="$real_base"
        if [[ ! -e "$STAGE_LIB_DIR/$real_base" ]]; then
            cp -L "$real_path" "$STAGE_LIB_DIR/$real_base"
            chmod 644 "$STAGE_LIB_DIR/$real_base"
            # 每个内置库都要有自己的 RUNPATH=$ORIGIN：DT_RUNPATH 不传递给传递依赖，
            # 不在每个库上设置时，Qt/OpenCV 的二级依赖仍会去系统路径找。
            patchelf --set-rpath '$ORIGIN' "$STAGE_LIB_DIR/$real_base"
            # 发行版库通常已剥离；本机自建 OpenCV/Qt 带符号，剥离可减体积。
            strip --strip-unneeded "$STAGE_LIB_DIR/$real_base" 2>/dev/null || true
            collect_dependencies "$STAGE_LIB_DIR/$real_base"
        fi
        # 关键：ELF 查找文件名用的是 SONAME（如 libopencv_world.so.414、
        # libQt5Core.so.5），而真实文件名常为 libopencv_world.so.4.14.0 /
        # libQt5Core.so.5.15.13。只拷真实文件时加载器在 $ORIGIN/lib 下找不到
        # SONAME，会静默回退到系统库，使“内置运行库”在未装 Qt/OpenCV 的机器上失效。
        if [[ "$soname" != "$real_base" && ! -e "$STAGE_LIB_DIR/$soname" ]]; then
            ln -s "$real_base" "$STAGE_LIB_DIR/$soname"
        fi
    done < <(ldd "$elf" 2>/dev/null | awk '/=> \//{print $1" "$3} /^[[:space:]]*\//{print $1" "$1}')
}

# 逐项校验：每个 NEEDED 要么属于基座/显示 ABI 库（由系统提供），要么在包内
# 能以同名文件（或 SONAME 软链）解析；不依赖构建机上是否装了同名系统库。
verify_needed_deps() {
    local elf="$1"
    local needed=""
    while read -r needed; do
        [[ -n "$needed" ]] || continue
        is_base_lib "$needed" && continue
        is_display_abi_lib "$needed" && continue
        if [[ ! -e "$STAGE_LIB_DIR/$needed" ]]; then
            echo "包内缺少依赖: $needed（来自 $elf）" >&2
            exit 1
        fi
    done < <(readelf -d "$elf" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
}

# 校验：主程序声明的每个非系统依赖，必须真的从包内目录加载，而不是仅在
# 构建机上靠系统已装的同名库才能启动（SONAME 与真实文件名不一致时最容易
# 出现“看似内置、实际未命中”的情况）。需要运行时 ld.so 日志才能判定。
verify_bundle_in_use() {
    local log="$1"
    local elf="$2"
    local needed=""
    while read -r needed; do
        [[ -n "$needed" ]] || continue
        is_base_lib "$needed" && continue
        is_display_abi_lib "$needed" && continue
        if ! grep -q "trying file=$STAGE_APP_DIR/lib/$needed$" "$log"; then
            echo "包内库未被加载器采用: $needed（检查 SONAME 软链与 RUNPATH）" >&2
            exit 1
        fi
    done < <(readelf -d "$elf" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
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
# 只剥离调试段（-g 产生的 DWARF），保留符号表，现场 gdb/backtrace 仍可读函数名。
strip --strip-debug "$STAGE_APP_DIR/ImageViewer" 2>/dev/null || true
collect_dependencies "$STAGE_APP_DIR/ImageViewer"

# ---- Qt 插件：平台/图片格式/输入法/GL 集成，随包提供并由 qt.conf 指向 ----
# platformthemes(gtk3) 不打包：它会把整套 GTK 依赖带进包内，且系统主题缺失时 Qt 会
# 自动回退到 Fusion，不影响使用。
QT_PLUGINS_DIR="$(qmake -query QT_INSTALL_PLUGINS)"
PLUGIN_GROUPS="platforms imageformats platforminputcontexts xcbglintegrations iconengines styles"
# SVG 图标引擎与 SVG 图片插件不打包：应用未链接 QtSvg，打包它们只会额外带入
# libQt5Svg 及依赖。其他图片格式（JPEG/PNG/TIFF/WebP/TGA…）照常内置。
is_skipped_plugin() {
    case "$(basename "$1")" in
        libqsvg.so|libqsvgicon.so) return 0 ;;
        *) return 1 ;;
    esac
}
for plugin_group in $PLUGIN_GROUPS; do
    if [[ ! -d "$QT_PLUGINS_DIR/$plugin_group" ]]; then
        echo "跳过不存在的插件目录: $plugin_group" >&2
        continue
    fi
    mkdir -p "$STAGE_APP_DIR/plugins/$plugin_group"
    cp -L "$QT_PLUGINS_DIR/$plugin_group/"*.so* "$STAGE_APP_DIR/plugins/$plugin_group/" 2>/dev/null || true
    for plugin in "$STAGE_APP_DIR/plugins/$plugin_group/"*; do
        [[ -e "$plugin" ]] || continue
        if is_skipped_plugin "$plugin"; then
            rm -f "$plugin"
            continue
        fi
        # 插件在 plugins/<group>/ 下，距 lib/ 是两级，必须是 $ORIGIN/../../lib。
        patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"
        chmod 644 "$plugin"
        strip --strip-unneeded "$plugin" 2>/dev/null || true
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

# ---- 一致性校验 ----
# 1) 无 not found；2) 每个 NEEDED（除系统提供的基础/显示库）在包内同名可解析。
# 3) 加载器实际采用包内库由下面的冒烟测试（LD_DEBUG）判定。
while IFS= read -r elf; do
    if ldd "$elf" 2>/dev/null | grep -q "not found"; then
        echo "依赖解析失败: $elf" >&2
        ldd "$elf" | grep "not found" >&2
        exit 1
    fi
    verify_needed_deps "$elf"
done < <(find "$STAGE_APP_DIR" -type f \( -name "ImageViewer" -o -name "*.so*" \))

echo "内置运行库数量: $(find "$STAGE_LIB_DIR" -maxdepth 1 -type f | wc -l)"

# 静态校验：主程序与插件对 Qt/OpenCV 的直接依赖均需解析到包内 lib/。
# 插件是 dlopen 的，主程序的 RUNPATH 不作用于它们，只能靠各自的 RUNPATH，
# 这里用 ldd（遵循被分析文件自身的 RUNPATH）做验证。
verify_resolution_in_bundle() {
    local elf="$1"
    local leaked=""
    leaked="$(ldd "$elf" 2>/dev/null | grep -E 'libQt5|libopencv' \
        | awk '/=> \//{print $3} /^[[:space:]]*\//{print $1}' \
        | while read -r resolved; do
            # 插件 RUNPATH 含 ../../，ldd 会原样拼出未规范的路径，先规范化再判断。
            if [[ "$(realpath -m "$resolved")" != "$STAGE_LIB_DIR"/* ]]; then
                echo "$resolved"
            fi
        done)"
    if [[ -n "$leaked" ]]; then
        echo "依赖未指向包内库（$elf）:" >&2
        echo "$leaked" >&2
        exit 1
    fi
}
verify_resolution_in_bundle "$STAGE_APP_DIR/ImageViewer"
while IFS= read -r plugin; do
    verify_resolution_in_bundle "$plugin"
done < <(find "$STAGE_APP_DIR/plugins" -type f -name "*.so")

# ---- 冒烟测试：用虚拟显示启动打包后的主程序，进程存活即认为依赖完整 ----
# 优先 xvfb（真正走 xcb 平台插件）；未安装 xvfb 时退为 offscreen，仍可验证
# 动态库与 qt.conf 是否能完成加载。
if [[ "$SMOKE_TEST" -eq 1 ]]; then
    SMOKE_RUNNER=()
    SMOKE_PLATFORM=""
    if command -v xvfb-run > /dev/null 2>&1; then
        SMOKE_RUNNER=(xvfb-run -a)
    else
        SMOKE_PLATFORM="offscreen"
        echo "未安装 xvfb-run，改用 offscreen 平台冒烟测试" >&2
    fi
    set +e
    SMOKE_LOG="$(mktemp)"
    # stdout 与 stderr 一并写入日志：xvfb-run 会把子进程的 stderr 合并到自己的
    # stdout，单独重定向 stderr 会丢失主程序的 ld.so 日志。
    if [[ -n "$SMOKE_PLATFORM" ]]; then
        LD_DEBUG=libs QT_QPA_PLATFORM="$SMOKE_PLATFORM" timeout 10 \
            "$STAGE_APP_DIR/ImageViewer" >"$SMOKE_LOG" 2>&1
    else
        LD_DEBUG=libs timeout 10 "${SMOKE_RUNNER[@]}" \
            "$STAGE_APP_DIR/ImageViewer" >"$SMOKE_LOG" 2>&1
    fi
    SMOKE_EXIT=$?
    set -e
    # 124 = timeout 杀掉了仍在运行的进程，说明窗口正常启动；1 可能是无图像参数的退出码。
    if [[ $SMOKE_EXIT -ne 124 && $SMOKE_EXIT -ne 1 && $SMOKE_EXIT -ne 0 ]]; then
        echo "冒烟测试失败（退出码 $SMOKE_EXIT），内置依赖可能不完整" >&2
        rm -f "$SMOKE_LOG"
        exit 1
    fi
    verify_bundle_in_use "$SMOKE_LOG" "$STAGE_APP_DIR/ImageViewer"
    rm -f "$SMOKE_LOG"
    echo "冒烟测试通过（退出码 $SMOKE_EXIT，已确认从包内加载 Qt/OpenCV）"
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
TryExec=ImageViewer
Icon=ImageViewer
Terminal=false
Categories=Graphics;Viewer;
Keywords=image;viewer;photo;opencv;pixel;
MimeType=image/png;image/jpeg;image/jpg;image/bmp;image/gif;image/tiff;image/webp;image/x-xpixmap;
StartupNotify=true
StartupWMClass=ImageViewer
EOF

# ---- 维护脚本：刷新图标缓存与桌面数据库，避免菜单/图标短期内不生效 ----
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: imageviewer
Version: $VERSION
Architecture: amd64
Maintainer: Blazar <blazarlin@gmail.com>
Depends: libc6 (>= 2.35), libgcc-s1, libstdc++6 (>= 12), libgl1, libegl1, libdrm2, libgbm1
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
# xz -9：Qt/ICU/OpenCV 体积大且重复模式多，最高压缩级别可再省几个百分点。
dpkg-deb --build --root-owner-group -Zxz -z9 "$STAGE" "$OUTPUT"
( cd "$OUTPUT_DIR" && sha256sum "$(basename "$OUTPUT")" > "$(basename "$OUTPUT").sha256" )
echo "已生成: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
echo "校验文件: $OUTPUT.sha256"
