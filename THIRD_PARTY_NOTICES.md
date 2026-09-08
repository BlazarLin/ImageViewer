# 第三方组件与再分发

ImageViewer 自有代码使用根目录 MIT 许可。`3rdpart/` 中的代码、导入库以及发布包中的第三方 DLL、插件与运行库保持各自原有许可；MIT 不覆盖或取代这些许可。

| 组件 | 使用方式 | 许可与来源 |
| --- | --- | --- |
| Qt Core/Gui/Widgets/Concurrent/Network/Svg、平台与图片插件 | 动态链接；本地 5.14.2，CI 配置 5.15.2 | 开源 LGPLv3/GPL 许可选项及各子组件许可，见 [Qt 说明](https://www.qt.io/development/open-source-lgpl-obligations)；随附 `licenses/qt/LICENSE.LGPL3`、`LICENSE.GPL3` |
| OpenCV 4.5.5 | 动态链接 opencv_world；图像处理与分析 | [Apache-2.0](https://opencv.org/license/)，原文见 `licenses/OpenCV-4.5.5.txt` |
| Qt/OpenCV 内含的图像编解码及基础库 | 取决于具体依赖构建选项 | 以对应版本源码中的 LICENSE、COPYING、NOTICE 和 Qt attribution 文件为准 |
| Microsoft Visual C++ Runtime | windeployqt 按需部署 | Microsoft 运行库再分发条款，不能按 MIT 重新授权 |

## 用户权利与源码

程序采用共享库部署；用户可以按相关许可证修改并替换兼容的 Qt 库。项目不对第三方库修改、替换或为调试此类修改进行的逆向工程施加额外限制。

- ImageViewer：[源码仓库](https://github.com/BlazarLin/ImageViewer)，每个公开版本应关联对应标签与源码包。
- Qt：[5.14.2 源码归档](https://download.qt.io/archive/qt/5.14/5.14.2/single/)、[5.15.2 源码归档](https://download.qt.io/archive/qt/5.15/5.15.2/single/)。按实际构建版本使用对应源码和许可证。
- OpenCV：[4.5.5 源码](https://github.com/opencv/opencv/tree/4.5.5)。自定义构建还需保留构建选项与任何修改。

以上上游地址方便查阅，不代表已经履行每份二进制的全部源码提供义务。维护者应在实际发布位置提供对应依赖源码的获取方式、构建信息及所需归属文件；若依赖有修改，应一并提供修改。参见 [发布说明](docs/RELEASING.md)。

## 当前仓库的历史依赖

`3rdpart/opencv` 保留了历史头文件和导入库，未含完整运行时与可重复构建记录。因此公共 CI 显式从 OpenCV 官方固定提交构建依赖，避免把开发机中的自定义 CUDA 等依赖误作为通用便携版。历史 Git 记录没有在本轮重写或移除。

`licenses/` 包含基础许可文本。打包脚本的 `-DependencySourceDir` 可收集源码树内的许可证和 attribution 文件，但它不是许可证合规分析器；正式发布前须核对所有实际随包 DLL/插件及其嵌入组件。
