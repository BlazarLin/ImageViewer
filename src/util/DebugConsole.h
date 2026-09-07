// 2026-09-07
// 功能：按用户设置动态显示或关闭 Windows 调试终端。
// 目的：默认保持纯 GUI 启动，同时保留现场日志诊断入口。
#pragma once

namespace util {

bool setDebugConsoleVisible(bool bVisible);

} // namespace util
