#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QAction;
class QLabel;
class QImage;
class ImageView;
QT_END_NAMESPACE

namespace ui {
class ImageView;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 程序化打开图片(命令行、IPC、最近文件)。
    void openFile(const QString& path);

protected:
    // 拖拽支持
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void onOpen();
    void onFitWindow();
    void onActualSize();
    void onAbout();

private:
    void setupActions();
    void setupStatusBar();
    void updateStatusBar();

    ui::ImageView* view_ = nullptr;

    QAction* actOpen_ = nullptr;
    QAction* actFit_ = nullptr;
    QAction* actActualSize_ = nullptr;
    QAction* actAbout_ = nullptr;
    QAction* actExit_ = nullptr;

    QLabel* statusFile_ = nullptr;
    QLabel* statusSize_ = nullptr;
    QLabel* statusZoom_ = nullptr;

    QString currentPath_;
};