#include "MainWindow.h"

#include "core/loader/ImageLoader.h"
#include "ui/ImageView.h"
#include "util/ElapsedLog.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>

namespace {
constexpr const char* kFilter =
    "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif);;All files (*.*)";
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString("ImageViewer"));
    resize(1280, 800);
    setAcceptDrops(true);

    view_ = new ui::ImageView(this);
    setCentralWidget(view_);

    setupActions();
    setupStatusBar();
}

void MainWindow::setupActions()
{
    actOpen_ = new QAction(tr("打开(&O)..."), this);
    actOpen_->setShortcut(QKeySequence::Open);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::onOpen);

    actFit_ = new QAction(tr("适应窗口(&F)"), this);
    actFit_->setShortcut(QKeySequence(QString("Ctrl+0")));
    connect(actFit_, &QAction::triggered, this, &MainWindow::onFitWindow);

    actActualSize_ = new QAction(tr("实际大小(&A)"), this);
    actActualSize_->setShortcut(QKeySequence(QString("Ctrl+1")));
    connect(actActualSize_, &QAction::triggered, this, &MainWindow::onActualSize);

    actAbout_ = new QAction(tr("关于(&A)"), this);
    connect(actAbout_, &QAction::triggered, this, &MainWindow::onAbout);

    actExit_ = new QAction(tr("退出(&X)"), this);
    actExit_->setShortcut(QKeySequence::Quit);
    connect(actExit_, &QAction::triggered, qApp, &QApplication::quit);

    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(actOpen_);
    fileMenu->addSeparator();
    fileMenu->addAction(actExit_);

    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(actFit_);
    viewMenu->addAction(actActualSize_);

    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(actAbout_);

    auto* tb = addToolBar(tr("主工具栏"));
    tb->setObjectName("MainToolBar");
    tb->addAction(actOpen_);
    tb->addSeparator();
    tb->addAction(actFit_);
    tb->addAction(actActualSize_);
}

void MainWindow::setupStatusBar()
{
    statusFile_ = new QLabel(this);
    statusSize_ = new QLabel(this);
    statusZoom_ = new QLabel(this);
    statusBar()->addWidget(statusFile_, 1);
    statusBar()->addPermanentWidget(statusSize_);
    statusBar()->addPermanentWidget(statusZoom_);
    updateStatusBar();
}

void MainWindow::updateStatusBar()
{
    if (currentPath_.isEmpty()) {
        statusFile_->setText(tr("就绪"));
    } else {
        statusFile_->setText(QFileInfo(currentPath_).fileName());
    }
    statusSize_->clear();
    statusZoom_->setText(QString("100%"));
}

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"), QString(), QString::fromUtf8(kFilter));
    if (path.isEmpty()) {
        return;
    }
    openFile(path);
}

void MainWindow::openFile(const QString& path)
{
    util::ElapsedLog _t(QString("MainWindow::openFile"));

    const auto result = core::loader::loadImage(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("打开失败"), result.error);
        qWarning().noquote() << "load failed:" << result.error;
        return;
    }

    view_->setImage(result.image);
    currentPath_ = path;
    updateStatusBar();
    setWindowTitle(QString("ImageViewer - %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::onFitWindow()
{
    if (!view_->image().isNull()) {
        view_->fitInView(view_->sceneRect(), Qt::KeepAspectRatio);
    }
}

void MainWindow::onActualSize()
{
    if (!view_->image().isNull()) {
        view_->resetTransform();
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("关于 ImageViewer"),
        tr("ImageViewer\n\n"
                       "M0 骨架版本:支持常见格式(jpg/png/bmp/tif/webp/gif)、"
                       "拖拽打开、命令行参数、适应窗口/实际大小。\n\n"
                       "计划详见项目根目录 plan 文件。"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    // 取第一张图片
    for (const QUrl& u : urls) {
        const QString path = u.toLocalFile();
        if (path.isEmpty()) {
            continue;
        }
        const QFileInfo fi(path);
        if (!fi.isFile()) {
            continue;
        }
        openFile(path);
        e->acceptProposedAction();
        return;
    }
}
