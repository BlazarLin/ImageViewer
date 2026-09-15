// 2026-09-06
// 功能：提供无边框主窗口的标题信息、拖动和窗口控制。
// 目的：以紧凑方式展示当前图像摘要，替代 Qt 原生标题栏。
#pragma once

#include <QWidget>
#include <QVector>

class QLabel;
class QToolButton;

namespace ui {

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);

    void setInfoText(const QString& text, const QString& fullPath);
    void setMaximized(bool bMaximized);
    // 在标题栏内添加菜单按钮（文件/视图/…），返回按钮供调用方绑定 QMenu。
    QToolButton* addMenuButton(const QString& text);
    QToolButton* menuButton(int nIndex) const;

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QLabel* iconLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QVector<QToolButton*> menuButtons_;
    QToolButton* minimizeButton_ = nullptr;
    QToolButton* maximizeButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    QPoint dragOffset_;
    bool bDragging_ = false;
};

} // namespace ui
