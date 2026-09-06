#include "MainWindow.h"

#include "core/loader/ImageLoader.h"
#include "core/analysis/ImageAnalysis.h"
#include "core/navigation/DirectoryModel.h"
#include "core/processing/ProcessingPreset.h"
#include "ui/ImageView.h"
#include "ui/AnalysisPanel.h"
#include "ui/PreprocessPanel.h"
#include "ui/ThumbnailBar.h"
#include "ui/TitleBar.h"
#include "util/ElapsedLog.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QCursor>
#include <QDockWidget>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageWriter>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <Windows.h>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , directoryModel_(std::make_unique<core::navigation::DirectoryModel>())
    , processingWatcher_(new QFutureWatcher<core::processing::ProcessingResult>(this))
    , analysisWatcher_(new QFutureWatcher<core::analysis::AnalysisResult>(this))
    , processingTimer_(new QTimer(this))
{
    setWindowTitle(QString("ImageViewer"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    resize(1320, 860);
    setMinimumSize(720, 500);
    setAcceptDrops(true);

    setupUi();
    setupActions();
    setupMenusAndToolbar();
    setupStatusBar();

    processingTimer_->setSingleShot(true);
    processingTimer_->setInterval(80);
    connect(processingTimer_, &QTimer::timeout, this, &MainWindow::startPreprocess);
    connect(processingWatcher_, &QFutureWatcher<core::processing::ProcessingResult>::finished,
        this, &MainWindow::onPreprocessFinished);
    connect(analysisWatcher_, &QFutureWatcher<core::analysis::AnalysisResult>::finished,
        this, &MainWindow::onRoiAnalysisFinished);
    updateNavigationActions();
    updateImageInformation();
}

MainWindow::~MainWindow()
{
    ++nProcessingGeneration_;
    ++nAnalysisGeneration_;
    processingWatcher_->waitForFinished();
    analysisWatcher_->waitForFinished();
}

void MainWindow::setupUi()
{
    titleBar_ = new ui::TitleBar(this);
    auto* topContainer = new QWidget(this);
    auto* topLayout = new QVBoxLayout(topContainer);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);
    topLayout->addWidget(titleBar_);
    auto* appMenuBar = new QMenuBar(topContainer);
    appMenuBar->setObjectName(QString("AppMenuBar"));
    topLayout->addWidget(appMenuBar);
    setMenuWidget(topContainer);

    connect(titleBar_, &ui::TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &ui::TitleBar::maximizeRestoreRequested, this, &MainWindow::toggleMaximized);
    connect(titleBar_, &ui::TitleBar::closeRequested, this, &QWidget::close);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    view_ = new ui::ImageView(central);
    thumbnailBar_ = new ui::ThumbnailBar(central);
    centralLayout->addWidget(view_, 1);
    centralLayout->addWidget(thumbnailBar_);
    setCentralWidget(central);

    preprocessPanel_ = new ui::PreprocessPanel(this);
    preprocessDock_ = new QDockWidget(tr("图像预处理"), this);
    preprocessDock_->setObjectName(QString("PreprocessDock"));
    preprocessDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    preprocessDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    preprocessDock_->setWidget(preprocessPanel_);
    addDockWidget(Qt::RightDockWidgetArea, preprocessDock_);
    preprocessDock_->hide();

    analysisPanel_ = new ui::AnalysisPanel(this);
    analysisDock_ = new QDockWidget(tr("像素与 ROI 分析"), this);
    analysisDock_->setObjectName(QString("AnalysisDock"));
    analysisDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    analysisDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    analysisDock_->setWidget(analysisPanel_);
    addDockWidget(Qt::RightDockWidgetArea, analysisDock_);
    analysisDock_->hide();

    setStyleSheet(QString(
        "QMainWindow { background:#2e3033; }"
        "QMenuBar { background:#242629; color:#ededed; padding:2px; }"
        "QMenuBar::item:selected,QMenu::item:selected { background:#3f657d; }"
        "QMenu { background:#2c2e32; color:#ededed; border:1px solid #44474c; }"
        "QToolBar { background:#25272a; border:0; spacing:4px; padding:4px; }"
        "QToolButton { color:#eeeeee; padding:5px; }"
        "QStatusBar { background:#222427; color:#d8d8d8; }"
        "QDockWidget { color:#eeeeee; }"));

    connect(view_, &ui::ImageView::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(view_, &ui::ImageView::pixelHovered, this,
        [this](const QPoint& position, const QColor& color, bool bValid) {
            if (analysisDock_->isVisible()) {
                analysisPanel_->setPixel(position, color, bValid);
            }
        });
    connect(view_, &ui::ImageView::roiSelected, this, &MainWindow::startRoiAnalysis);
    connect(thumbnailBar_, &ui::ThumbnailBar::fileActivated, this, &MainWindow::openFile);
    connect(preprocessPanel_, &ui::PreprocessPanel::parametersChanged,
        this, &MainWindow::onPreprocessParametersChanged);
    connect(preprocessPanel_, &ui::PreprocessPanel::resetRequested,
        this, &MainWindow::showOriginalImage);
    connect(preprocessDock_, &QDockWidget::visibilityChanged, this, [this](bool bVisible) {
        if (!bVisible) {
            preprocessPanel_->setProcessingEnabled(false);
            showOriginalImage();
        }
    });
}

void MainWindow::setupActions()
{
    actOpen_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开"), this);
    actOpen_->setShortcut(QKeySequence::Open);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::onOpen);

    actSaveResult_ = new QAction(tr("另存当前结果…"), this);
    actSaveResult_->setShortcut(QKeySequence(QString("Ctrl+Shift+S")));
    actSaveResult_->setEnabled(false);
    connect(actSaveResult_, &QAction::triggered, this, &MainWindow::onSaveResult);
    actSavePreset_ = new QAction(tr("保存预处理预设…"), this);
    connect(actSavePreset_, &QAction::triggered, this, &MainWindow::onSavePreset);
    actLoadPreset_ = new QAction(tr("载入预处理预设…"), this);
    connect(actLoadPreset_, &QAction::triggered, this, &MainWindow::onLoadPreset);

    actPrevious_ = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("上一张"), this);
    actPrevious_->setShortcut(QKeySequence(Qt::Key_Left));
    connect(actPrevious_, &QAction::triggered, this, &MainWindow::onPrevious);
    addAction(actPrevious_);

    actNext_ = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("下一张"), this);
    actNext_->setShortcut(QKeySequence(Qt::Key_Right));
    connect(actNext_, &QAction::triggered, this, &MainWindow::onNext);
    addAction(actNext_);

    actFit_ = new QAction(tr("适应窗口"), this);
    actFit_->setShortcut(QKeySequence(QString("Ctrl+0")));
    connect(actFit_, &QAction::triggered, view_, &ui::ImageView::fitToWindow);
    actFitWidth_ = new QAction(tr("适应宽度"), this);
    connect(actFitWidth_, &QAction::triggered, view_, &ui::ImageView::fitToWidth);
    actFitHeight_ = new QAction(tr("适应高度"), this);
    connect(actFitHeight_, &QAction::triggered, view_, &ui::ImageView::fitToHeight);
    actActualSize_ = new QAction(tr("实际大小"), this);
    actActualSize_->setShortcut(QKeySequence(QString("Ctrl+1")));
    connect(actActualSize_, &QAction::triggered, view_, &ui::ImageView::actualSize);

    actTogglePreprocess_ = preprocessDock_->toggleViewAction();
    actTogglePreprocess_->setText(tr("图像预处理"));
    actTogglePreprocess_->setShortcut(QKeySequence(QString("Ctrl+P")));
    actToggleAnalysis_ = analysisDock_->toggleViewAction();
    actToggleAnalysis_->setText(tr("像素与 ROI 分析"));
    actToggleAnalysis_->setShortcut(QKeySequence(QString("Ctrl+I")));
    actCompare_ = new QAction(tr("原图 / 处理图对比"), this);
    actCompare_->setCheckable(true);
    actCompare_->setEnabled(false);
    actCompare_->setShortcut(QKeySequence(QString("Ctrl+D")));
    connect(actCompare_, &QAction::toggled, this, &MainWindow::toggleComparison);
    actAbout_ = new QAction(tr("关于"), this);
    connect(actAbout_, &QAction::triggered, this, &MainWindow::onAbout);
    actExit_ = new QAction(tr("退出"), this);
    actExit_->setShortcut(QKeySequence::Quit);
    connect(actExit_, &QAction::triggered, qApp, &QApplication::quit);
}

void MainWindow::setupMenusAndToolbar()
{
    QWidget* topContainer = menuWidget();
    QMenuBar* appMenuBar = topContainer ? topContainer->findChild<QMenuBar*>(QString("AppMenuBar")) : nullptr;
    if (!appMenuBar) {
        return;
    }
    auto* fileMenu = appMenuBar->addMenu(tr("文件(&F)"));
    fileMenu->addAction(actOpen_);
    fileMenu->addAction(actSaveResult_);
    fileMenu->addSeparator();
    fileMenu->addAction(actSavePreset_);
    fileMenu->addAction(actLoadPreset_);
    fileMenu->addSeparator();
    fileMenu->addAction(actExit_);
    auto* viewMenu = appMenuBar->addMenu(tr("视图(&V)"));
    viewMenu->addAction(actFit_);
    viewMenu->addAction(actFitWidth_);
    viewMenu->addAction(actFitHeight_);
    viewMenu->addAction(actActualSize_);
    viewMenu->addSeparator();
    viewMenu->addAction(actTogglePreprocess_);
    viewMenu->addAction(actToggleAnalysis_);
    viewMenu->addAction(actCompare_);
    auto* languageMenu = appMenuBar->addMenu(tr("语言(&L)"));
    QAction* chineseAction = languageMenu->addAction(tr("简体中文"));
    QAction* englishAction = languageMenu->addAction(QString("English"));
    connect(chineseAction, &QAction::triggered, this, [this]() { selectLanguage(QString("zh_CN")); });
    connect(englishAction, &QAction::triggered, this, [this]() { selectLanguage(QString("en_US")); });
    auto* helpMenu = appMenuBar->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(actAbout_);

    auto* toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QString("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->addAction(actOpen_);
    toolbar->addAction(actSaveResult_);
    toolbar->addSeparator();
    toolbar->addAction(actPrevious_);
    toolbar->addAction(actNext_);
    toolbar->addSeparator();
    toolbar->addAction(actFit_);
    toolbar->addAction(actActualSize_);
    toolbar->addSeparator();
    toolbar->addAction(actTogglePreprocess_);
    toolbar->addAction(actToggleAnalysis_);
    toolbar->addAction(actCompare_);
}

void MainWindow::setupStatusBar()
{
    statusFile_ = new QLabel(this);
    statusSize_ = new QLabel(this);
    statusZoom_ = new QLabel(this);
    statusProcessing_ = new QLabel(this);
    statusBar()->addWidget(statusFile_, 1);
    statusBar()->addPermanentWidget(statusProcessing_);
    statusBar()->addPermanentWidget(statusSize_);
    statusBar()->addPermanentWidget(statusZoom_);
}

void MainWindow::openFile(const QString& path)
{
    util::ElapsedLog timer(QString("MainWindow::openFile"));
    const auto result = core::loader::loadImage(path);
    if (!result.ok()) {
        QMessageBox::warning(this, tr("打开失败"), result.error);
        qWarning().noquote() << tr("图像加载失败：") << result.error;
        return;
    }

    const QStringList oldFiles = directoryModel_->files();
    directoryModel_->loadForFile(QFileInfo(path).absoluteFilePath());
    currentPath_ = QFileInfo(path).absoluteFilePath();
    originalImage_ = result.image;
    processedImage_ = QImage();
    displayedImage_ = originalImage_;
    ++nProcessingGeneration_;
    ++nAnalysisGeneration_;
    bProcessingPending_ = false;
    bAnalysisPending_ = false;
    analysisPanel_->clear();
    actCompare_->setChecked(false);
    actCompare_->setEnabled(false);
    actSaveResult_->setEnabled(true);

    if (oldFiles != directoryModel_->files()) {
        thumbnailBar_->setFiles(directoryModel_->files(), directoryModel_->currentIndex());
    } else {
        thumbnailBar_->setCurrentFileIndex(directoryModel_->currentIndex());
    }
    if (processingParameters_.bEnabled) {
        bProcessingPending_ = true;
        processingTimer_->start();
    } else {
        showOriginalImage();
    }
    updateNavigationActions();
    updateImageInformation();
}

void MainWindow::onOpen()
{
    const QString filter = tr("图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif);;所有文件 (*.*)");
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"), QString(), filter);
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::onSaveResult()
{
    if (displayedImage_.isNull()) {
        return;
    }
    const QString suggestedName = currentPath_.isEmpty()
        ? QString("result.png")
        : QFileInfo(currentPath_).completeBaseName() + QString("_result.png");
    const QString directory = currentPath_.isEmpty()
        ? QString() : QFileInfo(currentPath_).absolutePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("另存当前结果"),
        QDir(directory).filePath(suggestedName),
        tr("PNG 图像 (*.png);;JPEG 图像 (*.jpg *.jpeg);;BMP 图像 (*.bmp);;TIFF 图像 (*.tif *.tiff);;WebP 图像 (*.webp)"));
    if (path.isEmpty()) {
        return;
    }
    QImageWriter writer(path);
    writer.setQuality(95);
    if (!writer.write(displayedImage_)) {
        QMessageBox::warning(this, tr("保存失败"), writer.errorString());
        return;
    }
    statusBar()->showMessage(tr("已保存：%1").arg(path), 4000);
}

void MainWindow::onSavePreset()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("保存预处理预设"),
        QString("ImageViewerPreset.json"), tr("JSON 预设 (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!core::processing::ProcessingPreset::save(path, processingParameters_, &error)) {
        QMessageBox::warning(this, tr("保存失败"), error);
        return;
    }
    statusBar()->showMessage(tr("预设已保存：%1").arg(path), 4000);
}

void MainWindow::onLoadPreset()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("载入预处理预设"),
        QString(), tr("JSON 预设 (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    core::processing::ProcessingParameters parameters;
    QString error;
    if (!core::processing::ProcessingPreset::load(path, &parameters, &error)) {
        QMessageBox::warning(this, tr("载入失败"), error);
        return;
    }
    preprocessPanel_->setParameters(parameters);
    preprocessDock_->show();
    statusBar()->showMessage(tr("预设已载入：%1").arg(path), 4000);
}

void MainWindow::onPrevious()
{
    if (directoryModel_->movePrevious()) {
        openFile(directoryModel_->currentPath());
    }
}

void MainWindow::onNext()
{
    if (directoryModel_->moveNext()) {
        openFile(directoryModel_->currentPath());
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("关于 ImageViewer"),
        tr("ImageViewer V1\n\n支持光标锚定缩放、像素网格、目录缩略图、"
           "自定义标题栏和 OpenCV 实时预处理。"));
}

void MainWindow::onZoomChanged(double factor)
{
    if (statusZoom_) {
        statusZoom_->setText(QString::number(factor * 100.0, 'f', factor < 0.1 ? 1 : 0) + QString("%"));
    }
    updateImageInformation();
}

void MainWindow::onPreprocessParametersChanged(
    const core::processing::ProcessingParameters& parameters)
{
    processingParameters_ = parameters;
    ++nProcessingGeneration_;
    if (!processingParameters_.bEnabled || originalImage_.isNull()) {
        processingTimer_->stop();
        bProcessingPending_ = false;
        showOriginalImage();
        return;
    }
    bProcessingPending_ = true;
    processingTimer_->start();
}

void MainWindow::startPreprocess()
{
    if (!processingParameters_.bEnabled || originalImage_.isNull()) {
        showOriginalImage();
        return;
    }
    if (processingWatcher_->isRunning()) {
        bProcessingPending_ = true;
        return;
    }
    bProcessingPending_ = false;
    nRunningGeneration_ = nProcessingGeneration_;
    const QImage source = originalImage_;
    const core::processing::ProcessingParameters parameters = processingParameters_;
    statusProcessing_->setText(tr("处理中…"));
    processingWatcher_->setFuture(QtConcurrent::run([source, parameters]() {
        return core::processing::ImageProcessor::process(source, parameters);
    }));
}

void MainWindow::onPreprocessFinished()
{
    const core::processing::ProcessingResult result = processingWatcher_->result();
    if (nRunningGeneration_ == nProcessingGeneration_ && processingParameters_.bEnabled) {
        if (result.ok()) {
            processedImage_ = result.image;
            displayedImage_ = processedImage_;
            actCompare_->setEnabled(true);
            view_->setComparisonImages(originalImage_, processedImage_, actCompare_->isChecked());
            statusProcessing_->setText(tr("预处理 %1 ms").arg(result.nElapsedMs));
        } else {
            statusProcessing_->setText(tr("预处理失败"));
            qWarning().noquote() << result.error;
        }
    }
    if (bProcessingPending_) {
        processingTimer_->start();
    }
}

void MainWindow::startRoiAnalysis(const QRect& region)
{
    if (displayedImage_.isNull() || region.isEmpty()) {
        return;
    }
    pendingAnalysisRegion_ = region.intersected(displayedImage_.rect());
    ++nAnalysisGeneration_;
    analysisPanel_->setSelection(pendingAnalysisRegion_);
    analysisPanel_->setBusy(true);
    if (analysisWatcher_->isRunning()) {
        bAnalysisPending_ = true;
        return;
    }
    bAnalysisPending_ = false;
    nRunningAnalysisGeneration_ = nAnalysisGeneration_;
    const QImage image = displayedImage_;
    const QRect requestedRegion = pendingAnalysisRegion_;
    analysisWatcher_->setFuture(QtConcurrent::run([image, requestedRegion]() {
        return core::analysis::ImageAnalysis::analyze(image, requestedRegion);
    }));
}

void MainWindow::onRoiAnalysisFinished()
{
    const core::analysis::AnalysisResult result = analysisWatcher_->result();
    if (nRunningAnalysisGeneration_ == nAnalysisGeneration_) {
        if (result.ok()) {
            analysisPanel_->setStatistics(result);
        } else {
            analysisPanel_->setBusy(false);
            qWarning().noquote() << result.error;
        }
    }
    if (bAnalysisPending_) {
        startRoiAnalysis(pendingAnalysisRegion_);
    }
}

void MainWindow::toggleComparison(bool bEnabled)
{
    if (processedImage_.isNull()) {
        if (actCompare_->isChecked()) {
            actCompare_->setChecked(false);
        }
        return;
    }
    view_->setComparisonImages(originalImage_, processedImage_, bEnabled);
}

void MainWindow::showOriginalImage()
{
    if (!originalImage_.isNull()) {
        displayedImage_ = originalImage_;
        processedImage_ = QImage();
        view_->setImage(originalImage_, false);
    }
    if (actCompare_) {
        actCompare_->setChecked(false);
        actCompare_->setEnabled(false);
    }
    if (statusProcessing_) {
        statusProcessing_->clear();
    }
}

void MainWindow::updateNavigationActions()
{
    const int nIndex = directoryModel_->currentIndex();
    actPrevious_->setEnabled(nIndex > 0);
    actNext_->setEnabled(nIndex >= 0 && nIndex + 1 < directoryModel_->count());
}

void MainWindow::updateImageInformation()
{
    if (!statusFile_ || !titleBar_) {
        return;
    }
    if (currentPath_.isEmpty() || originalImage_.isNull()) {
        statusFile_->setText(tr("就绪"));
        statusSize_->clear();
        if (statusZoom_) {
            statusZoom_->setText(QString("100%"));
        }
        titleBar_->setInfoText(tr("未打开图像  |  ImageViewer"), QString());
        return;
    }
    statusFile_->setText(QFileInfo(currentPath_).fileName());
    statusSize_->setText(QString("%1 × %2").arg(originalImage_.width()).arg(originalImage_.height()));
    titleBar_->setInfoText(formatTitleText(), currentPath_);
    setWindowTitle(QString("ImageViewer - %1").arg(QFileInfo(currentPath_).fileName()));
}

QString MainWindow::formatTitleText() const
{
    const QFileInfo info(currentPath_);
    const int nIndex = directoryModel_->currentIndex();
    return tr("%1  |  %2/%3 个文件  |  %4%  |  %5×%6  |  %7  |  %8  |  %9  |  ImageViewer")
        .arg(info.fileName())
        .arg(nIndex >= 0 ? nIndex + 1 : 0)
        .arg(directoryModel_->count())
        .arg(QString::number(view_->zoomFactor() * 100.0, 'f', view_->zoomFactor() < 0.1 ? 1 : 0))
        .arg(originalImage_.width()).arg(originalImage_.height())
        .arg(formatFileSize(info.size()))
        .arg(pixelFormatText(originalImage_))
        .arg(info.lastModified().toString(QString("yyyy/MM/dd HH:mm:ss")));
}

QString MainWindow::pixelFormatText(const QImage& image) const
{
    const int nDepth = image.depth();
    if (image.isGrayscale()) {
        return tr("灰度/%1-bit").arg(nDepth);
    }
    if (image.hasAlphaChannel()) {
        return tr("RGBA/%1-bit").arg(nDepth);
    }
    return tr("RGB/%1-bit").arg(nDepth);
}

QString MainWindow::formatFileSize(qint64 nBytes) const
{
    static const char* units[] = { "B", "KB", "MB", "GB" };
    double dValue = static_cast<double>(nBytes);
    int nUnit = 0;
    while (dValue >= 1024.0 && nUnit < 3) {
        dValue /= 1024.0;
        ++nUnit;
    }
    return QString("%1 %2").arg(QString::number(dValue, 'f', nUnit == 0 ? 0 : 2))
        .arg(QString::fromLatin1(units[nUnit]));
}

void MainWindow::toggleMaximized()
{
    isMaximized() ? showNormal() : showMaximized();
    titleBar_->setMaximized(isMaximized());
}

void MainWindow::selectLanguage(const QString& localeName)
{
    QSettings settings;
    settings.setValue(QString("ui/language"), localeName);
    QMessageBox::information(this, tr("语言设置"), tr("语言已保存，重启程序后生效。"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (QFileInfo(path).isFile()) {
            openFile(path);
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && titleBar_) {
        titleBar_->setMaximized(isMaximized());
    }
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    Q_UNUSED(eventType)
    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_NCHITTEST && !isMaximized()) {
        constexpr int nBorder = 6;
        const QPoint cursor = QCursor::pos();
        const QRect frame = frameGeometry();
        const bool bLeft = cursor.x() >= frame.left() && cursor.x() < frame.left() + nBorder;
        const bool bRight = cursor.x() <= frame.right() && cursor.x() > frame.right() - nBorder;
        const bool bTop = cursor.y() >= frame.top() && cursor.y() < frame.top() + nBorder;
        const bool bBottom = cursor.y() <= frame.bottom() && cursor.y() > frame.bottom() - nBorder;
        if (bTop && bLeft) { *result = HTTOPLEFT; return true; }
        if (bTop && bRight) { *result = HTTOPRIGHT; return true; }
        if (bBottom && bLeft) { *result = HTBOTTOMLEFT; return true; }
        if (bBottom && bRight) { *result = HTBOTTOMRIGHT; return true; }
        if (bLeft) { *result = HTLEFT; return true; }
        if (bRight) { *result = HTRIGHT; return true; }
        if (bTop) { *result = HTTOP; return true; }
        if (bBottom) { *result = HTBOTTOM; return true; }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
