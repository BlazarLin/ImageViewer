#pragma once

#include <QObject>
#include <QStringList>

class QApplication;
class QLocalServer;

// 2026-09-08：按用户/会话建立单实例本地管道，可靠转发文件并等待接收确认。
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QApplication* app, QObject* parent = nullptr);
    ~Application() override;

    // 解析命令行,返回待打开的图片路径列表。
    QStringList pendingFiles() const { return pendingFiles_; }

    // 应用启动入口。返回 true 表示应继续运行, false 应退出(已有实例接管)。
    bool startup();
    QString startupError() const { return startupError_; }

signals:
    void filesRequested(const QStringList& paths);

private:
    void parseArgs();

    QApplication* app_ = nullptr;
    QStringList pendingFiles_;

    QLocalServer* server_ = nullptr;
    QString startupError_;
    // 互斥避免 Windows 允许同名管道多服务端的启动竞争。
    void* hMutex_ = nullptr; // HANDLE
};