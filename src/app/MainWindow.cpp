#include "MainWindow.h"

#include "core/loader/ImageLoader.h"
#include "core/analysis/ImageAnalysis.h"
#include "core/navigation/DirectoryModel.h"
#include "core/processing/ProcessingPreset.h"
#include "app/AppVersion.h"
#include "ui/ImageView.h"
#include "ui/AnalysisPanel.h"
#include "ui/PreprocessPanel.h"
#include "ui/ThumbnailBar.h"
#include "ui/TitleBar.h"
#include "util/ElapsedLog.h"
#include "util/DebugConsole.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QUrl>
#include <QCloseEvent>
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
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <Windows.h>

#include <vector>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , loadWatcher_(new QFutureWatcher<core::loader::LoadResult>(this))
    , directoryModel_(std::make_unique<core::navigation::DirectoryModel>())
    , processingWatcher_(new QFutureWatcher<core::processing::ProcessingResult>(this))
    , analysisWatcher_(new QFutureWatcher<core::analysis::AnalysisResult>(this))
    , preloadWatcher_(new QFutureWatcher<QVector<PreloadResult>>(this))
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
    connect(preloadWatcher_, &QFutureWatcher<QVector<PreloadResult>>::finished,
        this, &MainWindow::onNeighborPreloadFinished);
    connect(loadWatcher_, &QFutureWatcher<core::loader::LoadResult>::finished,
        this, &MainWindow::onImageLoadFinished);
    QSettings settings;
    restoreGeometry(settings.value(QString("ui/geometry")).toByteArray());
    restoreState(settings.value(QString("ui/windowState")).toByteArray());
    updateNavigationActions();
    updateResultActions();
    updateImageInformation();
}

MainWindow::~MainWindow()
{
    ++nLoadGeneration_;
    loadWatcher_->waitForFinished();
    ++nProcessingGeneration_;
    ++nAnalysisGeneration_;
    processingWatcher_->waitForFinished();
    analysisWatcher_->waitForFinished();
    preloadWatcher_->waitForFinished();
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

    const auto installDockTitleBar = [this](QDockWidget* dock, const QString& title) {
        auto* titleBar = new QWidget(dock);
        titleBar->setObjectName(QString("DockTitleBar"));
        titleBar->setFixedHeight(44);
        auto* layout = new QHBoxLayout(titleBar);
        layout->setContentsMargins(12, 0, 4, 0);
        layout->setSpacing(6);
        auto* label = new QLabel(title, titleBar);
        label->setObjectName(QString("DockTitleLabel"));
        auto* closeButton = new QToolButton(titleBar);
        closeButton->setObjectName(QString("DockCloseButton"));
        closeButton->setText(QString("×"));
        closeButton->setToolTip(tr("关闭"));
        closeButton->setFixedSize(34, 30);
        closeButton->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(label, 1);
        layout->addWidget(closeButton);
        connect(closeButton, &QToolButton::clicked, dock, &QDockWidget::hide);
        dock->setTitleBarWidget(titleBar);
        dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    };

    preprocessPanel_ = new ui::PreprocessPanel(this);
    preprocessDock_ = new QDockWidget(tr("图像预处理"), this);
    preprocessDock_->setObjectName(QString("PreprocessDock"));
    preprocessDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    preprocessDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    preprocessDock_->setWidget(preprocessPanel_);
    installDockTitleBar(preprocessDock_, tr("图像预处理"));
    addDockWidget(Qt::RightDockWidgetArea, preprocessDock_);
    preprocessDock_->hide();

    analysisPanel_ = new ui::AnalysisPanel(this);
    analysisDock_ = new QDockWidget(tr("像素与 ROI 分析"), this);
    analysisDock_->setObjectName(QString("AnalysisDock"));
    analysisDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    analysisDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    analysisDock_->setWidget(analysisPanel_);
    installDockTitleBar(analysisDock_, tr("像素与 ROI 分析"));
    addDockWidget(Qt::RightDockWidgetArea, analysisDock_);
    analysisDock_->hide();

    setStyleSheet(QString(
        "QMainWindow { background:#2d3035; color:#e5e9ef; }"
        "QWidget { font-family:'Microsoft YaHei UI'; font-size:16px; }"
        "QWidget#CustomTitleBar { background:#1d1f22; border-bottom:1px solid #30343a; }"
        "QLabel#TitleInfo { color:#dce1e7; font-size:15px; }"
        "QLabel#AppIcon { background:#1479ad; color:white; border-radius:5px; font-weight:600; }"
        "QWidget#CustomTitleBar QToolButton { border:0; background:transparent; color:#e7ebef; font-size:18px; }"
        "QWidget#CustomTitleBar QToolButton:hover { background:#33373d; }"
        "QWidget#CustomTitleBar QToolButton#CloseButton:hover { background:#c42b1c; }"
        "QMenuBar { background:#222428; color:#dfe3e8; padding:2px 6px; border-bottom:1px solid #30343a; }"
        "QMenuBar::item { background:transparent; padding:5px 10px; border-radius:4px; }"
        "QMenuBar::item:selected { background:#353a41; color:white; }"
        "QMenuBar::item:pressed { background:#176b98; color:white; }"
        "QMenu { background:#282b30; color:#e4e8ed; border:1px solid #454a52; padding:6px; }"
        "QMenu::item { padding:7px 28px 7px 12px; border-radius:4px; }"
        "QMenu::item:selected { background:#176b98; color:white; }"
        "QMenu::item:disabled { color:#747a83; }"
        "QMenu::separator { height:1px; background:#41464d; margin:5px 8px; }"
        "QToolBar { background:#222428; border:0; border-bottom:1px solid #34383e; spacing:3px; padding:6px 8px; }"
        "QToolBar QToolButton { color:#dfe3e8; background:transparent; border:1px solid transparent; border-radius:5px; min-height:34px; padding:5px 12px; }"
        "QToolBar QToolButton:hover { background:#343941; border-color:#454b54; color:white; }"
        "QToolBar QToolButton:pressed { background:#1c5e82; }"
        "QToolBar QToolButton:checked { background:#176b98; border-color:#268bc0; color:white; }"
        "QToolBar QToolButton:disabled { color:#666c74; background:transparent; }"
        "QToolBar::separator { width:1px; background:#434850; margin:5px 6px; }"
        "QStatusBar { background:#222428; color:#b9c0c8; border:0; min-height:34px; }"
        "QStatusBar::item { border:0; background:transparent; }"
        "QStatusBar QLabel { color:#b9c0c8; padding:0 8px; border:0; background:transparent; }"
        "QStatusBar QLabel#StatusPixel { color:#d5e3ed; }"
        "QDockWidget { background:#25282d; color:#e5e9ef; border-left:1px solid #3a3f46; }"
        "QDockWidget::title { background:#22252a; color:#e5e9ef; padding:9px 10px; text-align:left; border-bottom:1px solid #3b4047; }"
        "QDockWidget::close-button,QDockWidget::float-button { border:0; background:transparent; padding:4px; }"
        "QDockWidget::close-button:hover,QDockWidget::float-button:hover { background:#3a3f46; }"
        "QWidget#DockTitleBar { background:#22252a; border-bottom:1px solid #3b4047; }"
        "QLabel#DockTitleLabel { color:#e5e9ef; font-weight:600; }"
        "QToolButton#DockCloseButton { color:#c9cfd6; background:transparent; border:0; border-radius:4px; font-size:18px; }"
        "QToolButton#DockCloseButton:hover { color:white; background:#c42b1c; }"
        "QWidget#PreprocessPanel,QWidget#AnalysisPanel,QWidget#AnalysisContent { background:#25282d; color:#e1e5ea; }"
        "QFrame#ProcessingSwitchCard { background:#2b3b46; border:1px solid #31586f; border-radius:7px; }"
        "QCheckBox#ProcessingSwitch { color:#edf4f8; font-weight:600; spacing:9px; }"
        "QWidget#PreprocessParameters { background:transparent; }"
        "QScrollArea#PreprocessScroll { background:transparent; border:0; }"
        "QWidget#PreprocessPanel QGroupBox,QWidget#AnalysisPanel QGroupBox { background:#2b2e33; border:1px solid #41464e; border-radius:7px; margin-top:13px; padding-top:7px; font-weight:600; }"
        "QWidget#PreprocessPanel QGroupBox::title,QWidget#AnalysisPanel QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 6px; color:#cfd5dc; background:#2b2e33; }"
        "QWidget#PreprocessPanel QLabel,QWidget#AnalysisPanel QLabel { color:#cbd1d8; font-weight:400; }"
        "QWidget#PreprocessPanel QLabel#ValueBadge { color:#dceaf2; background:#202328; border:1px solid #444a52; border-radius:4px; padding:3px 4px; }"
        "QComboBox,QSpinBox { color:#e4e8ed; background:#34383e; border:1px solid #50565f; border-radius:4px; padding:4px 8px; selection-background-color:#176b98; }"
        "QComboBox:hover,QSpinBox:hover { border-color:#6b747f; }"
        "QComboBox:focus,QSpinBox:focus { border-color:#2d9bd3; }"
        "QComboBox:disabled,QSpinBox:disabled { color:#6f757d; background:#2b2e33; border-color:#3d4147; }"
        "QComboBox QAbstractItemView { background:#2b2e33; color:#e4e8ed; border:1px solid #50565f; selection-background-color:#176b98; outline:0; }"
        "QSlider::groove:horizontal { height:4px; background:#474c54; border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:#258fc5; border-radius:2px; }"
        "QSlider::handle:horizontal { width:14px; margin:-5px 0; border-radius:7px; background:#dce7ed; border:2px solid #258fc5; }"
        "QSlider::handle:horizontal:hover { background:white; border-color:#45afe2; }"
        "QSlider:disabled { background:transparent; }"
        "QSlider::groove:horizontal:disabled { background:#393d43; }"
        "QSlider::sub-page:horizontal:disabled { background:#48515a; }"
        "QSlider::handle:horizontal:disabled { background:#626870; border-color:#484d54; }"
        "QPushButton#SecondaryButton { color:#cfd5dc; background:#30343a; border:1px solid #50565f; border-radius:5px; padding:6px 12px; }"
        "QPushButton#SecondaryButton:hover { color:white; background:#393e45; border-color:#69727d; }"
        "QListWidget#ThumbnailBar { background:#222428; color:#d7dce2; border:0; border-top:1px solid #34383e; padding:6px; outline:0; }"
        "QListWidget#ThumbnailBar::item { background:#292c31; color:#c8ced5; border:2px solid transparent; border-radius:6px; padding:3px; }"
        "QListWidget#ThumbnailBar::item:hover { background:#31363d; border-color:#4d555f; }"
        "QListWidget#ThumbnailBar::item:selected { background:#263f50; color:white; border-color:#2d9bd3; }"
        "QScrollBar { background:#24272b; border:0; }"
        "QScrollBar:vertical { width:11px; }"
        "QScrollBar:horizontal { height:11px; }"
        "QScrollBar::handle { background:#50565f; border-radius:5px; min-height:28px; min-width:28px; }"
        "QScrollBar::handle:hover { background:#68717c; }"
        "QScrollBar::add-page,QScrollBar::sub-page { background:transparent; }"
        "QScrollBar::add-line,QScrollBar::sub-line { width:0; height:0; }"
        "QScrollBar::up-arrow,QScrollBar::down-arrow,QScrollBar::left-arrow,QScrollBar::right-arrow { width:0; height:0; }"
        "QToolTip { color:#f0f2f4; background:#202328; border:1px solid #535a63; padding:5px; }"));

    connect(view_, &ui::ImageView::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(view_, &ui::ImageView::pixelHovered, this,
        [this](const QPoint& position, const QColor& color, bool bValid) {
            updatePixelStatus(position, color, bValid);
            if (analysisDock_->isVisible()) {
                analysisPanel_->setPixel(position, color, bValid);
            }
        });
    connect(view_, &ui::ImageView::roiSelected, this, &MainWindow::startRoiAnalysis);
    connect(view_, &ui::ImageView::fileDropped, this, &MainWindow::openFile);
    connect(thumbnailBar_, &ui::ThumbnailBar::fileActivated, this, &MainWindow::openFile);
    connect(preprocessPanel_, &ui::PreprocessPanel::parametersChanged,
        this, &MainWindow::onPreprocessParametersChanged);
    connect(preprocessPanel_, &ui::PreprocessPanel::resetRequested,
        this, &MainWindow::showOriginalImage);
    // 面板互斥显示以保留看图空间，显隐不改变预处理开关。
    connect(preprocessDock_, &QDockWidget::visibilityChanged, this, [this](bool bVisible) {
        if (bVisible) { analysisDock_->hide(); }
    });
    connect(analysisDock_, &QDockWidget::visibilityChanged, this, [this](bool bVisible) {
        if (bVisible) { preprocessDock_->hide(); }
    });
    connect(view_, &ui::ImageView::selectionCleared, this, &MainWindow::clearAnalysis);
    view_->setToolTip(tr("滚轮缩放；双击切换适配 / 100%；Shift + 拖动选择 ROI；Esc 清除选区"));
}

void MainWindow::setupActions()
{
    actOpen_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开"), this);
    actOpen_->setShortcut(QKeySequence::Open);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::onOpen);

    actOpenFolder_ = new QAction(tr("打开图像所在文件夹"), this);
    actOpenFolder_->setObjectName(QString("OpenContainingFolder"));
    actOpenFolder_->setEnabled(false);
    connect(actOpenFolder_, &QAction::triggered, this, &MainWindow::onOpenContainingFolder);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(this);
        menu.addAction(actOpenFolder_);
        menu.exec(view_->viewport()->mapToGlobal(position));
    });

    actRefresh_ = new QAction(tr("刷新图像和目录"), this);
    actRefresh_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(actRefresh_, &QAction::triggered, this, &MainWindow::onRefresh);

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
    view_->setNavigationActions(actPrevious_, actNext_);

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
    actShowConsole_ = new QAction(tr("显示调试终端"), this);
    actShowConsole_->setCheckable(true);
    actShowConsole_->setChecked(QSettings().value(QString("ui/showDebugConsole"), false).toBool());
    connect(actShowConsole_, &QAction::toggled, this, &MainWindow::setDebugConsoleVisible);
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
    fileMenu->addAction(actRefresh_);
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
    auto* settingsMenu = appMenuBar->addMenu(tr("设置(&S)"));
    settingsMenu->addAction(actShowConsole_);
    auto* helpMenu = appMenuBar->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(actAbout_);

    auto* toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QString("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(actOpen_);
    toolbar->addAction(actSaveResult_);
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
    statusFile_->setObjectName(QString("StatusFile"));
    statusFile_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusFile_->setMinimumWidth(0);
    statusPixel_ = new QLabel(this);
    statusPixel_->setObjectName(QString("StatusPixel"));
    statusPixel_->ensurePolished();
    statusPixel_->setMinimumWidth(statusPixel_->fontMetrics().horizontalAdvance(
        QString("X: 00000  Y: 00000   RGBA: 255, 255, 255, 255")) + 16);
    statusPixel_->setToolTip(tr("图像坐标从 0 开始；显示光标所在图像的 RGBA 值（0–255）"));
    updatePixelStatus(QPoint(), QColor(), false);
    statusSize_ = new QLabel(this);
    statusSize_->setObjectName(QString("StatusSize"));
    statusZoom_ = new QLabel(this);
    statusZoom_->setObjectName(QString("StatusZoom"));
    statusProcessing_ = new QLabel(this);
    statusProcessing_->setObjectName(QString("StatusProcessing"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(statusFile_, 1);
    statusBar()->addPermanentWidget(statusPixel_);
    statusBar()->addPermanentWidget(statusProcessing_);
    statusBar()->addPermanentWidget(statusSize_);
    statusBar()->addPermanentWidget(statusZoom_);
}

void MainWindow::updatePixelStatus(const QPoint& position, const QColor& color, bool bValid)
{
    if (!statusPixel_) {
        return;
    }
    const QString text = bValid
        ? QString("X: %1  Y: %2   RGBA: %3, %4, %5, %6")
            .arg(position.x()).arg(position.y()).arg(color.red()).arg(color.green())
            .arg(color.blue()).arg(color.alpha())
        : tr("坐标：—   RGBA：—");
    if (statusPixel_->text() != text) {
        statusPixel_->setText(text);
    }
}

void MainWindow::onOpenContainingFolder()
{
    if (currentPath_.isEmpty()) {
        return;
    }
    const QString directory = QFileInfo(currentPath_).absolutePath();
    if (!QDir(directory).exists()
        || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        statusBar()->showMessage(tr("无法打开图像所在文件夹：%1").arg(directory), 6000);
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateWindowShape();
}

void MainWindow::updateWindowShape()
{
    if (isMaximized() || isFullScreen()) {
        clearMask();
        return;
    }
    QPainterPath outline;
    outline.addRoundedRect(QRectF(rect()), 12.0, 12.0);
    setMask(QRegion(outline.toFillPolygon().toPolygon()));
}

void MainWindow::openFile(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    requestedPath_ = QFileInfo(path).absoluteFilePath();
    ++nLoadGeneration_;
    bLoading_ = true;
    statusBar()->showMessage(tr("正在打开：%1").arg(QFileInfo(path).fileName()));
    updateResultActions();
    // 只保留最后一次请求；最多一个前台解码任务，避免快速翻图堆积。
    if (!bLoadTaskActive_) {
        startImageLoad();
    }
}

void MainWindow::startImageLoad()
{
    bLoadTaskActive_ = true;
    runningLoadPath_ = requestedPath_;
    nRunningLoadGeneration_ = nLoadGeneration_;
    const QString path = runningLoadPath_;
    QImage cached;
    imageCache_.find(path, &cached);
    loadWatcher_->setFuture(QtConcurrent::run([path, cached]() {
        if (!cached.isNull()) {
            return core::loader::LoadResult{ cached, QString() };
        }
        return core::loader::loadImage(path);
    }));
}

void MainWindow::onImageLoadFinished()
{
    bLoadTaskActive_ = false;
    if (nRunningLoadGeneration_ != nLoadGeneration_) {
        startImageLoad();
        return;
    }
    bLoading_ = false;
    const auto result = loadWatcher_->result();
    if (!result.ok()) {
        thumbnailBar_->setCurrentFileIndex(directoryModel_->currentIndex());
        statusBar()->showMessage(tr("打开失败：%1").arg(result.error), 10000);
        updateResultActions();
        return;
    }
    imageCache_.insert(runningLoadPath_, result.image);
    statusBar()->clearMessage();
    applyLoadedImage(runningLoadPath_, result.image);
}

void MainWindow::applyLoadedImage(const QString& path, const QImage& image)
{
    const QStringList oldFiles = directoryModel_->files();
    directoryModel_->loadForFile(QFileInfo(path).absoluteFilePath());
    currentPath_ = QFileInfo(path).absoluteFilePath();
    originalImage_ = image;
    processedImage_ = QImage();
    displayedImage_ = originalImage_;
    ++nProcessingGeneration_;
    ++nAnalysisGeneration_;
    bProcessingPending_ = false;
    bAnalysisPending_ = false;
    analysisPanel_->clear();
    actCompare_->setChecked(false);
    actCompare_->setEnabled(false);
    view_->setImage(originalImage_);
    statusProcessing_->clear();
    QSettings().setValue(QString("files/lastDirectory"), QFileInfo(path).absolutePath());

    if (oldFiles != directoryModel_->files()) {
        thumbnailBar_->setFiles(directoryModel_->files(), directoryModel_->currentIndex());
    } else {
        thumbnailBar_->setCurrentFileIndex(directoryModel_->currentIndex());
    }
    if (processingParameters_.bEnabled) {
        bProcessingPending_ = true;
        statusProcessing_->setText(tr("等待预处理…"));
        processingTimer_->start();
    } else {
        showOriginalImage();
    }
    updateNavigationActions();
    updateImageInformation();
    updateResultActions();
    scheduleNeighborPreload();
}

void MainWindow::onOpen()
{
    const QString filter = tr("图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif);;所有文件 (*.*)");
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"),
        QSettings().value(QString("files/lastDirectory")).toString(), filter);
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::onSaveResult()
{
    if (!actSaveResult_->isEnabled() || displayedImage_.isNull()) {
        return;
    }
    const QImage imageToSave = displayedImage_;
    const QString suggestedName = currentPath_.isEmpty()
        ? QString("result.png")
        : QFileInfo(currentPath_).completeBaseName() + QString("_result.png");
    const QString directory = currentPath_.isEmpty()
        ? QString() : QFileInfo(currentPath_).absolutePath();
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("另存当前结果"),
        QDir(directory).filePath(suggestedName),
        tr("PNG 图像 (*.png);;JPEG 图像 (*.jpg *.jpeg);;BMP 图像 (*.bmp);;TIFF 图像 (*.tif *.tiff);;WebP 图像 (*.webp)"), &selectedFilter);
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        const int nStart = selectedFilter.indexOf(QString("*."));
        const QString suffix = nStart >= 0
            ? selectedFilter.mid(nStart + 2).section(' ', 0, 0).section(')', 0, 0) : QString("png");
        path += QString(".") + suffix;
        if (QFileInfo::exists(path) && QMessageBox::question(this, tr("确认覆盖"),
                tr("文件已存在，是否覆盖？\n%1").arg(path)) != QMessageBox::Yes) {
            return;
        }
    }
    QString error;
    if (!core::loader::saveImage(path, imageToSave, &error)) {
        QMessageBox::warning(this, tr("保存失败"), error);
        return;
    }
    imageCache_.clear();
    directoryModel_->loadForFile(currentPath_, true);
    thumbnailBar_->setFiles(directoryModel_->files(), directoryModel_->currentIndex());
    updateNavigationActions();
    updateImageInformation();
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
    const int nRequestedIndex = bLoading_ ? directoryModel_->files().indexOf(requestedPath_) : -1;
    const int nIndex = nRequestedIndex >= 0 ? nRequestedIndex : directoryModel_->currentIndex();
    if (nIndex > 0) {
        openFile(directoryModel_->files().at(nIndex - 1));
    }
}

void MainWindow::onNext()
{
    const int nRequestedIndex = bLoading_ ? directoryModel_->files().indexOf(requestedPath_) : -1;
    const int nIndex = nRequestedIndex >= 0 ? nRequestedIndex : directoryModel_->currentIndex();
    if (nIndex >= 0 && nIndex + 1 < directoryModel_->count()) {
        openFile(directoryModel_->files().at(nIndex + 1));
    }
}

void MainWindow::onRefresh()
{
    if (currentPath_.isEmpty()) {
        return;
    }
    const int nPreviousIndex = directoryModel_->currentIndex();
    imageCache_.clear();
    directoryModel_->loadForFile(currentPath_, true);
    thumbnailBar_->setFiles(directoryModel_->files(), directoryModel_->currentIndex());
    updateNavigationActions();
    updateImageInformation();
    if (directoryModel_->currentIndex() < 0 && directoryModel_->count() > 0) {
        openFile(directoryModel_->files().at(std::clamp(nPreviousIndex, 0, directoryModel_->count() - 1)));
    } else {
        openFile(currentPath_);
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("关于 ImageViewer"),
        tr("ImageViewer v%1\n\n支持光标锚定缩放、像素网格、目录缩略图、"
           "自定义标题栏、实时预处理、ROI 分析、分割对比和大图显示金字塔。")
            .arg(app::version()));
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
    // 参数一旦变化就作废旧结果，避免把旧参数结果导出。
    view_->clearSelection();
    actCompare_->setChecked(false);
    actCompare_->setEnabled(false);
    statusProcessing_->setText(tr("等待预处理…"));
    processingTimer_->start();
    updateResultActions();
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
    actSaveResult_->setEnabled(false);
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
            clearAnalysis();
            actCompare_->setEnabled(true);
            view_->setComparisonImages(originalImage_, processedImage_, actCompare_->isChecked());
            statusProcessing_->setText(tr("预处理 %1 ms").arg(result.nElapsedMs));
        } else {
            showOriginalImage();
            statusProcessing_->setText(tr("预处理失败"));
            statusBar()->showMessage(result.error, 10000);
            qWarning().noquote() << result.error;
        }
    }
    if (bProcessingPending_) {
        processingTimer_->start();
    }
    updateResultActions();
}

void MainWindow::startRoiAnalysis(const QRect& region)
{
    if (displayedImage_.isNull() || region.isEmpty() || bLoading_
        || bProcessingPending_ || processingTimer_->isActive()
        || (processingParameters_.bEnabled && processingWatcher_->isRunning())) {
        return;
    }
    pendingAnalysisRegion_ = region.intersected(displayedImage_.rect());
    if (pendingAnalysisRegion_.isEmpty()) { return; }
    analysisDock_->show();
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
    view_->setComparisonEnabled(bEnabled);
}

void MainWindow::scheduleNeighborPreload()
{
    if (preloadWatcher_->isRunning()) {
        bPreloadPending_ = true;
        return;
    }
    const int nCurrentIndex = directoryModel_->currentIndex();
    if (nCurrentIndex < 0) {
        return;
    }
    std::vector<QString> requests;
    requests.reserve(4);
    for (int nDistance = 1; nDistance <= 2; ++nDistance) {
        const int candidates[] = { nCurrentIndex + nDistance, nCurrentIndex - nDistance };
        for (int nIndex : candidates) {
            if (nIndex < 0 || nIndex >= directoryModel_->count()) {
                continue;
            }
            const QString path = directoryModel_->files().at(nIndex);
            QImage cached;
            if (!imageCache_.find(path, &cached)) {
                requests.push_back(path);
            }
        }
    }
    if (requests.empty()) {
        bPreloadPending_ = false;
        return;
    }
    bPreloadPending_ = false;
    preloadWatcher_->setFuture(QtConcurrent::run([requests]() {
        QVector<PreloadResult> results;
        results.reserve(static_cast<int>(requests.size()));
        for (const QString& path : requests) {
            const core::loader::LoadResult loaded = core::loader::loadImage(path);
            if (loaded.ok()) {
                results.push_back({ path, loaded.image });
            }
        }
        return results;
    }));
}

void MainWindow::onNeighborPreloadFinished()
{
    const QVector<PreloadResult> results = preloadWatcher_->result();
    for (const PreloadResult& result : results) {
        imageCache_.insert(result.path, result.image);
    }
    if (bPreloadPending_) {
        scheduleNeighborPreload();
    }
}

void MainWindow::showOriginalImage()
{
    clearAnalysis();
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
    updateResultActions();
}

void MainWindow::clearAnalysis()
{
    ++nAnalysisGeneration_;
    bAnalysisPending_ = false;
    pendingAnalysisRegion_ = QRect();
    analysisPanel_->clear();
}

void MainWindow::updateResultActions()
{
    const bool bReady = !bLoading_ && !displayedImage_.isNull()
        && (!processingParameters_.bEnabled || (!bProcessingPending_
            && !processingTimer_->isActive() && !processingWatcher_->isRunning()));
    actSaveResult_->setEnabled(bReady);
    actCompare_->setEnabled(bReady && !processedImage_.isNull());
    actRefresh_->setEnabled(!currentPath_.isEmpty() && !bLoading_);
    actOpenFolder_->setEnabled(!currentPath_.isEmpty() && !bLoading_);
    for (QAction* action : { actFit_, actFitWidth_, actFitHeight_, actActualSize_ }) {
        action->setEnabled(!displayedImage_.isNull());
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue(QString("ui/geometry"), saveGeometry());
    settings.setValue(QString("ui/windowState"), saveState());
    QMainWindow::closeEvent(event);
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
        titleBar_->setInfoText(tr("未打开图像  |  ImageViewer v%1").arg(app::version()), QString());
        return;
    }
    statusFile_->setText(QFileInfo(currentPath_).fileName());
    statusFile_->setToolTip(currentPath_);
    statusSize_->setText(QString("%1 × %2").arg(originalImage_.width()).arg(originalImage_.height()));
    titleBar_->setInfoText(formatTitleText(), currentPath_);
    setWindowTitle(QString("ImageViewer - %1").arg(QFileInfo(currentPath_).fileName()));
}

QString MainWindow::formatTitleText() const
{
    const QFileInfo info(currentPath_);
    const int nIndex = directoryModel_->currentIndex();
    return tr("%1  |  %2/%3 个文件  |  %4%  |  %5×%6  |  %7  |  %8  |  %9  |  ImageViewer v%10")
        .arg(info.fileName())
        .arg(nIndex >= 0 ? nIndex + 1 : 0)
        .arg(directoryModel_->count())
        .arg(QString::number(view_->zoomFactor() * 100.0, 'f', view_->zoomFactor() < 0.1 ? 1 : 0))
        .arg(originalImage_.width()).arg(originalImage_.height())
        .arg(formatFileSize(info.size()))
        .arg(pixelFormatText(originalImage_))
        .arg(info.lastModified().toString(QString("yyyy/MM/dd HH:mm:ss")))
        .arg(app::version());
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

void MainWindow::setDebugConsoleVisible(bool bVisible)
{
    if (!util::setDebugConsoleVisible(bVisible)) {
        const QSignalBlocker blocker(actShowConsole_);
        actShowConsole_->setChecked(!bVisible);
        QMessageBox::warning(this, tr("设置失败"), tr("无法切换调试终端。"));
        return;
    }
    QSettings settings;
    settings.setValue(QString("ui/showDebugConsole"), bVisible);
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
        updateWindowShape();
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
