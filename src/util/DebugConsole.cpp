// 2026-09-07
// 功能：实现 Windows 控制台的动态创建、标准流绑定和释放。
// 目的：让调试终端成为可选功能，而不是每次启动都伴随出现。
#include "DebugConsole.h"

#include <Windows.h>

#include <cstdio>

namespace util {

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

} // namespace util
