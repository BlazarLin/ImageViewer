#pragma once

#include <QObject>
#include <QStringList>

class QApplication;

// 2026-10-27：解析启动文件；每次启动均使用独立进程和独立看图窗口。
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QApplication* app, QObject* parent = nullptr);
    ~Application() override;

    // 解析命令行,返回待打开的图片路径列表。
    QStringList pendingFiles() const { return pendingFiles_; }

    // 保留 --new-window 参数兼容旧快捷方式；当前所有启动均独立运行。
    bool isNewWindow() const { return bNewWindow_; }

    // 应用启动入口。多进程模式下始终继续运行当前进程。
    bool startup();
    QString startupError() const { return startupError_; }

private:
    void parseArgs();

    QApplication* app_ = nullptr;
    QStringList pendingFiles_;
    bool bNewWindow_ = false;
    QString startupError_;
};