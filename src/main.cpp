// =============================================================================
// ImageViewer - 入口
// 目标:VS2019 + Qt 5.14 + OpenCV 4.5.5 / Windows / x64 / Unicode
// =============================================================================

#include <Windows.h>
#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QLocale>
#include <QSettings>
#include <QTextCodec>
#include <QTranslator>
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
    QCoreApplication::setOrganizationName(QString("ImageViewer"));
    QCoreApplication::setApplicationName(QString("ImageViewer"));

    QSettings settings;
    const QString defaultLanguage = QLocale::system().name().startsWith(QString("zh"))
        ? QString("zh_CN") : QString("en_US");
    const QString language = settings.value(QString("ui/language"), defaultLanguage).toString();
    QTranslator translator;
    if (language != QString("zh_CN")) {
        const QString translationPath = QDir(QCoreApplication::applicationDirPath())
            .filePath(QString("translations/ImageViewer_%1.qm").arg(language));
        if (translator.load(translationPath)) {
            app.installTranslator(&translator);
        } else {
            qWarning().noquote() << "translation file not found:" << translationPath;
        }
    }

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
