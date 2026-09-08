// 2026-09-07
// 功能：集中定义 ImageViewer 的对外版本号。
// 目的：确保标题栏、关于窗口和运行时元数据使用同一版本。
#pragma once

#include <QString>

namespace app {

inline QString version()
{
    return QString("2.2.0");
}

} // namespace app
