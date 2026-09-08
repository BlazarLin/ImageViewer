# 参与贡献

欢迎中文或英文 Issue 和 Pull Request。请先阅读 [README](README.md) 的构建和已知限制。

1. Fork 仓库，从 `main` 创建功能分支。
2. 通过环境变量 `QTDIR`、`OPENCV_DIR` 或忽略的 `cmake/Config.local.props` 配置依赖，不提交本机路径。
3. 保持 C++17、四空格缩进、RAII 和现有命名风格。界面文案使用 `tr()`，同步维护 `translations/ImageViewer_en_US.ts`，不引入 `QStringLiteral`。
4. 添加必要的行为回归；执行 Debug、Release 测试及编码检查。视觉变更提供前后截图，说明 DPI 比例、窗口大小和手工验证范围。
5. 使用 `feat:`、`fix:`、`perf:`、`build:` 或 `docs:` 提交前缀。PR 描述用户可见变化、验证结果和影响范围。

提交贡献表示你有权按本仓库 MIT 许可提供该贡献；第三方代码必须保留原许可及归属信息。不要提交未经授权的图片、个人数据、依赖二进制、凭据、IDE 状态和构建目录。优先使用程序生成的测试图。

首次构建不需要 Qt VS Tools；工程和 CMake 都会生成 MOC。GitHub Actions 使用独立安装的 Qt 与从源码编译的 OpenCV，避免依赖开发机上的 DLL。
