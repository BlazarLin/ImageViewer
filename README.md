# ImageViewer

本地图像查看器 EXE:办公 + 工业视觉观察混用。常见图片浏览 + 大图(4K/8K/工业相机)流畅缩放,可叠加简单 OpenCV 预处理算子用于查看效果。

- 技术栈:**C++17 / Qt 5.14.2 / OpenCV 4.5.5**
- 平台:**Windows 10/11 x64 / VS2019 (v142)**
- 计划详见:项目根目录由 LLM 维护的 plan 文件

## 当前进度

**M0 骨架(完成)**:CMake + VS2019 双工程,可打开单图、拖拽、命令行参数、菜单/工具栏/状态栏、适应窗口/实际大小、Qt 单实例 IPC 占位。

后续节点:M1 缩放、M2 目录翻页、M3 大图金字塔、M4 预处理算子、M5 EXIF/全屏/幻灯片、M6 性能与发布。详见 plan。

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
│   │   └─ MainWindow.ui         # 占位
│   ├─ ui/
│   │   └─ ImageView.{h,cpp}     # QGraphicsView 子类
│   ├─ core/
│   │   └─ loader/
│   │       └─ ImageLoader.{h,cpp}
│   └─ util/
│       └─ ElapsedLog.h
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

支持多文件(只打开第一张,M2 实现翻页):

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

## 已知限制(M0 阶段)

- 缩放仅"适应窗口"和"实际大小"两个预设,**完整滚轮缩放/以光标为锚**留到 M1
- 大图未做瓦片金字塔,**8K+ 内存可能吃紧**,留到 M3
- 目录翻页未做,留到 M2
- 预处理算子未做,留到 M4
- Qt 单实例 IPC 已占位但接收回调未连接 MainWindow,留到 V1

## 关键测试(M0 阶段手动跑)

| 用例 | 期望 |
|------|------|
| 启动 EXE(无参数) | 空白主窗口,启动 <2s |
| `ImageViewer.exe test.png` | 打开图片,标题显示文件名 |
| 拖拽 jpg/png/bmp/tif/webp 到窗口 | 正常打开 |
| 拖拽损坏文件 | 弹错误框不崩 |
| 菜单 视图→适应窗口/实际大小 | 图正确缩放 |
| 启动另一个 EXE 实例 | 第一实例继续运行,第二实例退出 |
| 中文路径图片 | 正常打开不乱码 |