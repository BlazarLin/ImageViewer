# ImageViewer

本地图像查看器 EXE:办公 + 工业视觉观察混用。常见图片浏览 + 大图(4K/8K/工业相机)流畅缩放,可叠加简单 OpenCV 预处理算子用于查看效果。

- 技术栈:**C++17 / Qt 5.14.2 / OpenCV 4.5.5**
- 平台:**Windows 10/11 x64 / VS2019 (v142)**
- 设计与计划详见 `docs/superpowers/`

## 当前进度

**V2 工业图像查看与分析（完成）**：

- 打开、拖拽、命令行加载常见图像，支持中文路径。
- 以光标为锚点的滚轮缩放，范围 `1%~6400%`；`800%` 起切换最近邻并显示像素网格。
- 适应窗口、适应宽度、适应高度和实际大小。
- 当前目录底部缩略图栏，支持点击、左右键和工具栏切图。
- 无边框自定义标题栏，显示文件名、目录序号、缩放比、尺寸、大小、像素信息和修改时间。
- 可显隐的右侧实时预处理面板，默认隐藏。
- Qt `tr()` + `.ts/.qm` 多语言基础，内置简体中文和 English，切换后重启生效。
- 默认隐藏的像素/ROI 分析面板：RGB、灰度、HSV、Lab、通道统计与 256 档直方图。
- 原图/处理图单视图分割对比，分割线可拖动且共享缩放、平移坐标。
- 当前处理结果另存，预处理参数以版本化 JSON 预设保存和载入。
- 前后各两张邻图后台预加载，384 MiB 受限解码缓存和 256 MiB 持久化缩略图缓存。
- 超过 2048 像素的大图后台生成有限显示金字塔，高倍率自动切回原图层。

## 目录结构

```
CodeProject/
├─ ImageViewer.sln               # VS2019 解决方案(双击打开即可)
├─ CMakeLists.txt                # 备选 CMake 工程
├─ cmake/
│   └─ Config.props              # Qt / OpenCV 路径配置
├─ src/
│   ├─ ImageViewer.vcxproj       # VS2019 主工程
│   ├─ ImageViewer.vcxproj.filters
│   ├─ main.cpp
│   ├─ app/
│   │   ├─ Application.{h,cpp}   # 单实例 + 命令行
│   │   ├─ MainWindow.{h,cpp}    # 主窗口
│   │   └─ MainWindow.ui         # Qt Designer 兼容占位
│   ├─ ui/
│   │   ├─ ImageView.{h,cpp}     # 缩放、平移与像素网格
│   │   ├─ ThumbnailBar.{h,cpp}  # 异步缩略图导航
│   │   ├─ TitleBar.{h,cpp}      # 自定义标题栏
│   │   ├─ PreprocessPanel.{h,cpp}
│   │   └─ AnalysisPanel.{h,cpp}
│   ├─ core/
│   │   ├─ loader/ImageLoader.{h,cpp}
│   │   ├─ navigation/DirectoryModel.{h,cpp}
│   │   ├─ processing/ImageProcessor.{h,cpp}
│   │   ├─ processing/ProcessingPreset.{h,cpp}
│   │   ├─ analysis/ImageAnalysis.{h,cpp}
│   │   └─ cache/                # 解码、缩略图与显示金字塔缓存
│   └─ util/
│       └─ ElapsedLog.h
├─ tests/                         # 独立核心测试 EXE
├─ translations/                  # Qt Linguist 翻译源文件
└─ README.md
```

## VS2019 一键打开

### 前置依赖

1. **Visual Studio 2019**(v142 工具集,Windows SDK 10.0)
2. **Qt 5.14.2 for Windows (MSVC 2017 64-bit)**
3. **OpenCV 4.5.5**(已编译版本,需要 `build/x64/vc15/lib` 与 `build/include`)
4. **Qt VS Tools 插件**(VS2019 扩展市场,用于处理 moc/rcc/uic)

### 配置第三方路径

打开 `cmake/Config.props`,按本机实际路径修改默认 `QTDIR` 和 `OPENCV_DIR`,或设置同名环境变量:

```xml
<QTDIR>C:\Qt\Qt5.14.2\msvc2017_64</QTDIR>
<OPENCV_DIR>D:\opencv\build</OPENCV_DIR>
```

或者(推荐)用环境变量,不污染工程文件:

```powershell
setx QTDIR "C:\Qt\Qt5.14.2\msvc2017_64"
setx OPENCV_DIR "D:\opencv\build"
```

### 打开与运行

1. 双击 `ImageViewer.sln`,VS2019 加载解决方案
2. 配置选择 **Debug | x64**(首次需要)
3. 右键 `ImageViewer` → **设为启动项目**
4. 按 **F5** 开始调试

构建成功后,vs 会自动把 Qt/OpenCV 运行时 DLL 复制到 `bin/Debug/` 或 `bin/Release/`。

### 命令行运行

```
bin\Debug\ImageViewer.exe D:\test\big_image.tif
```

命令行可传入多文件，程序打开第一张后自动载入该目录用于翻页：

```
ImageViewer.exe img1.png img2.jpg
```

### 拖拽运行

直接把图片拖到 EXE 图标上,或拖入已打开的窗口。

## CMake 备选路径

如不熟悉 VS 工程,可用 CMake:

```powershell
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_PREFIX_PATH=C:/Qt/Qt5.14.2/msvc2017_64 ^
    -DOpenCV_DIR=D:/opencv/build
cmake --build . --config Release
```

## 实时预处理

使用 `Ctrl+P` 显示或隐藏面板。面板包含：

- 亮度、对比度、Gamma、灰度和 B/G/R 通道查看。
- 均值、高斯、中值滤波和 Unsharp Mask 锐化。
- 固定阈值、Otsu、Sobel、Laplacian 和 Canny。
- 腐蚀、膨胀、开运算和闭运算。

参数连续变化会在 80 ms 合并后交给后台线程处理，仅最新代次的结果能刷新界面。隐藏面板或复原参数会回到原图，不修改原文件。

## 像素与 ROI 分析

使用 `Ctrl+I` 显示或隐藏分析面板。鼠标在图像上移动时实时显示像素信息；按住 `Shift` 后用左键拖动选择 ROI，通道统计和直方图在后台计算。处理结果变化时旧统计会自动清空，避免将旧 ROI 数据误认为当前结果。

启用预处理并得到有效结果后，可用 `Ctrl+D` 打开原图/处理图分割对比并拖动黄色分割线。像素取样在分割线左侧读取原图、右侧读取处理图。

## 快捷键

| 操作 | 快捷键 |
|---|---|
| 打开图片 | `Ctrl+O` |
| 上一张 / 下一张 | `Left` / `Right` |
| 适应窗口 | `Ctrl+0` |
| 实际大小 | `Ctrl+1` |
| 显示/隐藏预处理 | `Ctrl+P` |
| 显示/隐藏像素与 ROI 分析 | `Ctrl+I` |
| 原图/处理图分割对比 | `Ctrl+D` |
| 另存当前结果 | `Ctrl+Shift+S` |

## 已知限制

- 当前为已解码图像的显示金字塔，尚未实现 TIFF/BigTIFF 编码器级按瓦片局部解码；解码峰值内存仍取决于原始格式。
- GIF/WebP 和多页 TIFF 当前只显示解码器返回的首帧。
- 当前预处理只提供 8 位预览，高位深工业图暂未保留 10/12/16 位定量精度。
- 单实例已限制重复启动，但第二实例的新文件参数尚未转发给第一实例。

## 构建与测试

VS2019 解决方案内含独立 `ImageViewerCoreTests` EXE，Debug/Release 均会构建：

```powershell
scripts\build.ps1 -Cfg Debug
bin\Tests\Debug\ImageViewerCoreTests.exe
```

核心测试使用中文业务日志，当前覆盖目录过滤/自然排序、原图直通、亮度饱和、Otsu/核归一化、RGB/BGR、ROI 统计、预设校验、缓存失效和金字塔尺寸。

## 关键手动验收

| 用例 | 期望 |
|------|------|
| 启动 EXE(无参数) | 空白主窗口，预处理面板默认隐藏 |
| `ImageViewer.exe test.png` | 打开图片，标题显示文件和图像信息，底部显示目录缩略图 |
| 拖拽 jpg/png/bmp/tif/webp 到窗口 | 正常打开 |
| 拖拽损坏文件 | 弹错误框不崩 |
| 滚轮与菜单缩放 | 光标锚点稳定，800% 显示像素格，四周背景为统一深灰 |
| 打开预处理并拖动参数 | 界面不卡死，只刷新最新结果，隐藏面板后恢复原图 |
| `Shift` + 左键框选 ROI | 面板异步显示范围、通道统计和直方图 |
| 启用分割对比并拖动黄线 | 左侧原图、右侧处理图，缩放和平移保持一致 |
| 另存结果并重开 | 像素内容与当前处理结果一致 |
| 启动另一个 EXE 实例 | 第一实例继续运行,第二实例退出 |
| 中文路径图片 | 正常打开不乱码 |
