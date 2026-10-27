#include "Application.h"

#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

Application::Application(QApplication* app, QObject* parent)
    : QObject(parent)
    , app_(app)
{
}

Application::~Application() = default;

void Application::parseArgs()
{
    if (!app_) {
        return;
    }
    const QStringList args = app_->arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QString("--new-window") || a == QString("-n")) {
            bNewWindow_ = true;
            continue;
        }
        if (a.startsWith('-')) {
            continue;
        }
        QString path = a;
        if (path.startsWith(QString("file://"))) {
            path = QUrl(path).toLocalFile();
        }
        if (QFileInfo(path).isFile()) {
            const QString absolute = QFileInfo(path).absoluteFilePath();
            if (!pendingFiles_.contains(absolute, Qt::CaseInsensitive)) {
                pendingFiles_.append(absolute);
            }
        } else {
            qWarning() << "skip non-existent path:" << path;
        }
    }
}

bool Application::startup()
{
    parseArgs();
    // 不建立单实例锁，也不转发或激活已有窗口。系统每次打开图片都会
    // 启动独立进程，由当前进程创建自己的窗口并读取自己的命令行文件。
    return true;
}
