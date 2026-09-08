// =============================================================================
// ImageViewer - 入口
// 目标:VS2019 + Qt 5.14 + OpenCV 4.5.5 / Windows / x64 / Unicode
// =============================================================================

#include <Windows.h>
#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QLocale>
#include <QSettings>
#include <QTextCodec>
#include <QTranslator>
#include <QtGlobal>

#include <cstdio>

#include "app/Application.h"
#include "app/AppVersion.h"
#include "app/MainWindow.h"
#include "ui/AppIcon.h"
#include "util/DebugConsole.h"

int main(int argc, char* argv[])
{
    // 控制台输出 UTF-8,避免中文路径/日志乱码(GUI 子系统也保留以读取 qDebug)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QString("ImageViewer"));
    QCoreApplication::setApplicationName(QString("ImageViewer"));
    QCoreApplication::setApplicationVersion(app::version());
    QApplication::setWindowIcon(ui::createAppIcon());

    QSettings settings;
    util::setDebugConsoleVisible(settings.value(QString("ui/showDebugConsole"), false).toBool());
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

    // qInfo 默认输出到 stderr；需要现场诊断时可在“设置”中显示调试终端。
    qInfo() << "ImageViewer starting. Qt" << qVersion();

    Application appl(&app);
    if (!appl.startup()) {
        if (!appl.startupError().isEmpty()) {
            QMessageBox::warning(nullptr, QString("ImageViewer"), appl.startupError());
            return 1;
        }
        // 已有实例确认接管,本进程退出
        return 0;
    }

    MainWindow w;
    QObject::connect(&appl, &Application::filesRequested, &w, [&w](const QStringList& paths) {
        if (w.isMinimized()) { w.setWindowState(w.windowState() & ~Qt::WindowMinimized); }
        w.show();
        w.raise();
        w.activateWindow();
        w.openFiles(paths);
    });
    w.show();

    // 命令行传入的文件
    const QStringList files = appl.pendingFiles();
    if (!files.isEmpty()) {
        w.openFiles(files);
    }

    return app.exec();
}
