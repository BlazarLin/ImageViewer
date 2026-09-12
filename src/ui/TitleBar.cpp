// 2026-09-06
// 功能：实现自定义标题栏布局、窗口按钮和拖动交互。
// 目的：保留 Windows 常用窗口操作的同时提高图像信息密度。
#include "TitleBar.h"
#include "AppIcon.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QStyle>
#include <QToolButton>

namespace ui {

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QString("CustomTitleBar"));
    // 单行标题栏：图标、菜单栏、图像信息与窗口按钮同行展示。
    setFixedHeight(38);

    iconLabel_ = new QLabel(this);
    iconLabel_->setAlignment(Qt::AlignCenter);
    iconLabel_->setFixedSize(22, 22);
    iconLabel_->setObjectName(QString("AppIcon"));
    iconLabel_->setPixmap(createAppIcon().pixmap(22, 22));
    iconLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    infoLabel_ = new QLabel(tr("未打开图像  |  ImageViewer"), this);
    infoLabel_->setObjectName(QString("TitleInfo"));
    infoLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    infoLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    minimizeButton_ = new QToolButton(this);
    maximizeButton_ = new QToolButton(this);
    closeButton_ = new QToolButton(this);
    const QList<QToolButton*> buttons = { minimizeButton_, maximizeButton_, closeButton_ };
    for (QToolButton* button : buttons) {
        button->setAutoRaise(true);
        button->setFixedSize(46, 38);
        button->setIconSize(QSize(13, 13));
        button->setFocusPolicy(Qt::NoFocus);
    }
    minimizeButton_->setText(QString("—"));
    minimizeButton_->setToolTip(tr("最小化"));
    closeButton_->setText(QString("×"));
    closeButton_->setToolTip(tr("关闭"));
    closeButton_->setObjectName(QString("CloseButton"));
    setMaximized(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(iconLabel_);
    layout->addWidget(infoLabel_, 1);
    layout->addWidget(minimizeButton_);
    layout->addWidget(maximizeButton_);
    layout->addWidget(closeButton_);

    connect(minimizeButton_, &QToolButton::clicked, this, &TitleBar::minimizeRequested);
    connect(maximizeButton_, &QToolButton::clicked, this, &TitleBar::maximizeRestoreRequested);
    connect(closeButton_, &QToolButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setMenuBar(QMenuBar* menuBar)
{
    if (!menuBar || !layout()) {
        return;
    }
    menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // 插在图标之后、信息标签之前，保持单行布局。
    dynamic_cast<QHBoxLayout*>(layout())->insertWidget(1, menuBar);
}

void TitleBar::setInfoText(const QString& text, const QString& fullPath)
{
    infoLabel_->setText(text);
    infoLabel_->setToolTip(fullPath);
}

void TitleBar::setMaximized(bool bMaximized)
{
    maximizeButton_->setText(bMaximized ? QString("❐") : QString("□"));
    maximizeButton_->setToolTip(bMaximized ? tr("还原") : tr("最大化"));
}

void TitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !window()->isMaximized()) {
        dragOffset_ = event->globalPos() - window()->frameGeometry().topLeft();
        bDragging_ = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (bDragging_ && (event->buttons() & Qt::LeftButton) && !window()->isMaximized()) {
        window()->move(event->globalPos() - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    bDragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit maximizeRestoreRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace ui
