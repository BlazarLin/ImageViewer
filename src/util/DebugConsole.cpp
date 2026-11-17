// 2026-09-07
// 功能：实现调试控制台的动态创建、标准流绑定和释放(Windows)。
// 目的：让调试终端成为可选功能，而不是每次启动都伴随出现。
// 2026-12-11
// 变更：Linux 适配。GUI 程序在 Linux 下默认无控制台句柄，从终端启动时
// stdout/stderr 已天然可用，桌面启动则无处输出，无需 AllocConsole。
#include "DebugConsole.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <Windows.h>

#include <cstdio>
#endif

namespace util {

#ifdef Q_OS_WIN

bool setDebugConsoleVisible(bool bVisible)
{
    HWND hConsoleWindow = ::GetConsoleWindow();
    if (!bVisible) {
        if (!hConsoleWindow) {
            return true;
        }
        std::fflush(stdout);
        std::fflush(stderr);
        return ::FreeConsole() != FALSE;
    }

    if (!hConsoleWindow && ::AllocConsole() == FALSE) {
        return false;
    }
    FILE* outputStream = nullptr;
    FILE* errorStream = nullptr;
    FILE* inputStream = nullptr;
    freopen_s(&outputStream, "CONOUT$", "w", stdout);
    freopen_s(&errorStream, "CONOUT$", "w", stderr);
    freopen_s(&inputStream, "CONIN$", "r", stdin);
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    return true;
}

#else

// Linux 无 AllocConsole 概念：从终端启动时输出已附着，桌面启动时静默丢弃。
bool setDebugConsoleVisible(bool bVisible)
{
    Q_UNUSED(bVisible)
    return true;
}

#endif

} // namespace util
