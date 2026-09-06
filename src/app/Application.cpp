#include "Application.h"

#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#include <Windows.h>

namespace {
constexpr const char* kMutexName = "Global\\ImageViewer.SingleInstance";
}

Application::Application(QApplication* app, QObject* parent)
    : QObject(parent)
    , app_(app)
{
}

Application::~Application()
{
    if (hMutex_) {
        ::CloseHandle(reinterpret_cast<HANDLE>(hMutex_));
        hMutex_ = nullptr;
    }
}

void Application::parseArgs()
{
    if (!app_) {
        return;
    }
    const QStringList args = app_->arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a.startsWith('-')) {
            continue;
        }
        QString path = a;
        if (path.startsWith(QString("file://"))) {
            path = QUrl(path).toLocalFile();
        }
        if (QFileInfo(path).isFile()) {
            pendingFiles_.append(path);
        } else {
            qWarning() << "skip non-existent path:" << path;
        }
    }
}

bool Application::startup()
{
    parseArgs();

    // 单实例:已有实例运行时直接退出(M0 不做参数转发,V1 再加)。
    const QString mu = QString::fromLatin1(kMutexName);
    hMutex_ = ::CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(mu.utf16()));
    if (!hMutex_) {
        qWarning() << "CreateMutex failed:" << ::GetLastError();
        return true; // 创建失败也允许继续运行
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        qInfo() << "another instance already running, exit.";
        ::CloseHandle(reinterpret_cast<HANDLE>(hMutex_));
        hMutex_ = nullptr;
        return false;
    }

    return true;
}
