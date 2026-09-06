#pragma once

#include <QObject>
#include <QStringList>

class QApplication;

// 单实例 + 命令行参数。
// M0 阶段:用 Windows CreateMutex 实现单实例(MFC/SDK 风格,免去引入 QtNetwork 模块)。
// V1 可换回 QLocalServer / QSharedMemory。
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QApplication* app, QObject* parent = nullptr);
    ~Application() override;

    // 解析命令行,返回待打开的图片路径列表。
    QStringList pendingFiles() const { return pendingFiles_; }

    // 应用启动入口。返回 true 表示应继续运行, false 应退出(已有实例接管)。
    bool startup();

private:
    void parseArgs();

    QApplication* app_ = nullptr;
    QStringList pendingFiles_;

    // 单实例互斥(M0:CreateMutex)
    void* hMutex_ = nullptr; // HANDLE
};