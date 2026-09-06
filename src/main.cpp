// =============================================================================
// ImageViewer - 入口
// 目标:VS2019 + Qt 5.14 + OpenCV 4.5.5 / Windows / x64 / Unicode
// =============================================================================

#include <Windows.h>
#include <QApplication>
#include <QDebug>
#include <QTextCodec>
#include <QtGlobal>

#include <cstdio>

#include "app/Application.h"
#include "app/MainWindow.h"

int main(int argc, char* argv[])
{
    // 控制台输出 UTF-8,避免中文路径/日志乱码(GUI 子系统也保留以读取 qDebug)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ImageViewer"));
    QCoreApplication::setApplicationName(QStringLiteral("ImageViewer"));

    // qInfo 默认会输出到 stderr(GUI 子系统下可能不可见)
    // 如果想要调试输出,改为 qInstallMessageHandler 自定义到文件。
    qInfo() << "ImageViewer starting. Qt" << qVersion();

    Application appl(&app);
    if (!appl.startup()) {
        // 已有实例接管,本进程退出
        return 0;
    }

    MainWindow w;
    w.show();

    // 命令行传入的文件
    const QStringList files = appl.pendingFiles();
    if (!files.isEmpty()) {
        w.openFile(files.first());
    }

    return app.exec();
}