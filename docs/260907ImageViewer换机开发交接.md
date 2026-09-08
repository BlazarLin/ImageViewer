# ImageViewer 换机开发交接

## 2026-09-08 最新续开发

方向键、作者信息、彩色读数、叠加直方图和 37 项回归见 [深度体验与优化建议](260908深度体验与优化建议.md)。开源配置沿用 2.2.0 未发布版本，当前 UI 以本轮截图为准。

## 2026-09-07 续开发记录

本轮继续开发的 10 项优化、换机依赖补全和 23 项回归验收见 [产品优化与验收记录](260907产品优化与验收记录.md)。需集中回复的产品决策见 [产品待回复事项](260907产品待回复事项.md)。下文保留原交接基线，运行结果以续开发记录为准。

## 交接目标

在另一台 Windows 开发机继续 `ImageViewer` 的功能开发与桌面端验收。工程仓库当前使用 `main` 分支。

## Git 基线

- 功能实现提交：`bd475c1 feat: 完善查看器交互、缩略图加载与应用设置`
- 上游基线：编写本文时 `github/main` 停在 `9cb486b`
- 当前工作区：交接文档提交后应保持干净
- 注意：功能实现及本文档目前均需推送。换机前应在源电脑执行 `git push github main`，或完整复制含 `.git` 的仓库。

本轮代码细节直接查看提交 `bd475c1`，不要从本文复制实现。主要入口：

- `src/ui/ThumbnailBar.cpp`
- `src/ui/ImageView.cpp`
- `src/ui/TitleBar.cpp`
- `src/app/MainWindow.cpp`
- `src/util/DebugConsole.cpp`
- `src/app/AppVersion.h`
- `src/ui/AppIcon.h`

## 已完成并验证

- 缩略图改为“当前项优先、可视区小批次加载、邻近少量预取”，避免首次打开大量并发解码；文件名独立显示在深色文字行。
- 自定义标题栏整段可拖动；主视图使用深色棋盘格空白背景；`ImageView` 已接入本地图片拖放信号链。
- 新应用图标已用于标题栏、窗口和任务栏。
- 版本号集中为 `2.1.0`，显示在空状态、图片信息标题和关于窗口。
- VS 工程改为 Windows GUI 子系统；设置菜单新增“显示调试终端”，默认关闭并通过 `QSettings` 持久化。
- 中文界面正常；英文翻译生成结果为 123 条完成、0 条未完成。
- VS2019 Debug、Release 构建通过；CMake Release 构建通过。
- Debug、Release 的 `ImageViewerCoreTests.exe` 均返回 0。
- Debug、Release 均生成 EXE 与应用 PDB。
- 视觉验收确认首屏当前缩略图优先出现、可视缩略图随后补齐、图标和 `v2.1.0` 正常；终端开关开启/关闭及复启默认无终端已验证。

## 下一台电脑首先处理

1. 获取含 `bd475c1` 及本文档提交的仓库后，确认 `git status -sb` 为干净状态。
2. 检查 `cmake/Config.props` 所引用的 Qt 5.14、OpenCV 4.5.5、VS2019 v142 路径是否适合新电脑；不要提交仅适用于个人机器的路径改动。
3. 执行：

   ```powershell
   .\scripts\build.ps1 -Cfg Debug
   .\scripts\build.ps1 -Cfg Release
   .\bin\Tests\Debug\ImageViewerCoreTests.exe
   .\bin\Tests\Release\ImageViewerCoreTests.exe
   ```

4. 补做唯一未闭环的桌面验收：从资源管理器把 PNG/JPEG/BMP 图片直接拖到主图区域，确认图片立即显示并更新同目录缩略图。代码链路为 `ImageView::dropEvent` → `fileDropped` → `MainWindow::openFile`。上一轮实测启动时被人工按 Esc 中断，不应把“已编译”当成该交互已通过。
5. 顺带复查设置菜单中“显示调试终端”默认未勾选；开关一次并重启，确认持久化行为一致。

## 已知非阻塞事项

- Release 链接出现第三方 `qtmain.lib` 缺少 `qtmain.pdb` 的 `LNK4099` 警告，但应用自身 `ImageViewer.pdb` 已正常生成，构建返回 0。
- 当前本地提交尚未推送到远端；这是换机前最容易遗漏的步骤。

## Suggested skills

- `computer-use:computer-use`：在新电脑上完成真实资源管理器拖放、终端开关和视觉一致性验收。
- `brainstorming`：继续增加或改变功能前，先确认交互目标与性能边界。
- `writing-plans`：后续涉及多模块的开发任务先形成可执行计划。
- `karpathy-guidelines`：保持修改范围收敛，并为每项改动定义可验证的完成标准。
