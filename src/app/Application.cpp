#include "Application.h"

#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <Windows.h>

Application::Application(QApplication* app, QObject* parent)
    : QObject(parent)
    , app_(app)
{
}

Application::~Application()
{
    if (server_) { server_->close(); }
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
            const QString absolute = QFileInfo(path).absoluteFilePath();
            if (!pendingFiles_.contains(absolute, Qt::CaseInsensitive)) { pendingFiles_.append(absolute); }
        } else {
            qWarning() << "skip non-existent path:" << path;
        }
    }
}

bool Application::startup()
{
    parseArgs();

    DWORD nSessionId = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &nSessionId);
    const QByteArray identity = (QDir::homePath() + QString("/")
        + QString::number(nSessionId) + QString("/") + QCoreApplication::applicationName()).toUtf8();
    const QString name = QString("ImageViewer-") + QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(24));
    const QString mutexName = QString("Local\\") + name;
    hMutex_ = ::CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(mutexName.utf16()));
    if (!hMutex_) {
        startupError_ = tr("无法建立单实例锁，请稍后重试。");
        return false;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        QLocalSocket socket;
        QElapsedTimer timer;
        timer.start();
        // 主进程可能仍在创建管道；有界重试避免双击启动竞争。
        while (timer.elapsed() < 3000) {
            socket.connectToServer(name);
            if (socket.waitForConnected(200)) { break; }
            socket.abort();
            QThread::msleep(40);
        }
        QJsonArray files;
        for (const QString& path : pendingFiles_) { files.append(path); }
        QByteArray message = QJsonDocument(files).toJson(QJsonDocument::Compact);
        message.append('\n');
        if (socket.state() == QLocalSocket::ConnectedState && message.size() <= 1024 * 1024) {
            ULONG nServerPid = 0;
            if (::GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &nServerPid)) {
                ::AllowSetForegroundWindow(nServerPid);
            }
            socket.write(message);
            if ((socket.bytesToWrite() == 0 || socket.waitForBytesWritten(1000))
                && (socket.bytesAvailable() > 0 || socket.waitForReadyRead(3000))
                && socket.readAll().startsWith("OK")) {
                return false;
            }
        }
        startupError_ = tr("已有窗口未确认接收文件，请等待其响应后重试。");
        return false;
    }

    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    connect(server_, &QLocalServer::newConnection, this, [this]() {
        while (server_->hasPendingConnections()) {
            auto* socket = server_->nextPendingConnection();
            auto* timeout = new QTimer(socket);
            timeout->setSingleShot(true);
            connect(timeout, &QTimer::timeout, socket, &QLocalSocket::abort);
            timeout->start(5000);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            socket->setReadBufferSize(1024 * 1024 + 1);
            const auto receive = [this, socket, timeout]() {
                if (socket->property("received").toBool()) { return; }
                if (socket->bytesAvailable() > 1024 * 1024) { socket->abort(); return; }
                if (!socket->canReadLine()) { return; }
                const auto document = QJsonDocument::fromJson(socket->readLine());
                if (!document.isArray()) { socket->abort(); return; }
                QStringList paths;
                for (const auto& value : document.array()) {
                    if (!value.isString()) { socket->abort(); return; }
                    paths.append(value.toString());
                }
                socket->setProperty("received", true);
                timeout->stop();
                emit filesRequested(paths);
                socket->write("OK\n");
                socket->flush();
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, this, receive);
            receive();
        }
    });
    if (!server_->listen(name)) {
        startupError_ = tr("无法建立文件转发通道：%1").arg(server_->errorString());
        return false;
    }
    return true;
}
