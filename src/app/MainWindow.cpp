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
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QPainter>
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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QDialog>
#include <QProcess>
#include <QPushButton>
#include <QMimeData>
#include <QSettings>
#include <QScreen>
#include <QShowEvent>
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
#include <QImageReader>
#include <QSet>
#include <algorithm>

// 2026-10-27：无边框窗口顶层描边层。子控件会覆盖主窗口自身绘制的边框，
// 因此用透明且不接收鼠标的覆盖层统一绘制外框，保证四边一致可见。
class WindowFrameOverlay final : public QWidget {
public:
    explicit WindowFrameOverlay(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QString("WindowFrameOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

    void setActive(bool bActive)
    {
        bActive_ = bActive;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!bActive_) { return; }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // 用内外圆角路径相减生成 1px 描边环：直边落在整像素上保持清晰，
        // 圆角由抗锯齿平滑；描边整体内缩 1px，不会被窗口圆角遮罩裁掉。
        constexpr qreal dRadius = 12.0;
        const auto ring = [](const QRectF& outerRect, qreal dOuterRadius,
                             const QRectF& innerRect, qreal dInnerRadius) {
            QPainterPath outerPath;
            outerPath.addRoundedRect(outerRect, dOuterRadius, dOuterRadius);
            QPainterPath innerPath;
            innerPath.addRoundedRect(innerRect, dInnerRadius, dInnerRadius);
            return outerPath.subtracted(innerPath);
        };
        painter.fillPath(ring(QRectF(rect()).adjusted(1, 1, -1, -1), dRadius - 1,
            QRectF(rect()).adjusted(2, 2, -2, -2), dRadius - 2),
            QColor(124, 141, 156));
        painter.fillPath(ring(QRectF(rect()).adjusted(2, 2, -2, -2), dRadius - 2,
            QRectF(rect()).adjusted(3, 3, -3, -3), dRadius - 3),
            QColor(12, 16, 20));
    }

private:
    bool bActive_ = true;
};

namespace {

// 六通道卡片只保存有界缩略图，尺寸变化时保持宽高比。
class ChannelThumbnailLabel final : public QLabel {
public:
    explicit ChannelThumbnailLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(80, 60);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setTextInteractionFlags(Qt::NoTextInteraction);
    }

    void setImage(const QImage& image)
    {
        image_ = image;
        updatePixmap();
    }

    void setMessage(const QString& message)
    {
        image_ = QImage();
        setPixmap(QPixmap());
        setText(message);
    }

    const QImage& image() const { return image_; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updatePixmap();
    }

private:
    void updatePixmap()
    {
        if (image_.isNull() || width() <= 0 || height() <= 0) { return; }
        if (!text().isEmpty()) { setText(QString()); }
        setPixmap(QPixmap::fromImage(image_).scaled(size(), Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    }

    QImage image_;
};

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , loadWatcher_(new QFutureWatcher<core::loader::LoadResult>(this))
    , directoryModel_(std::make_unique<core::navigation::DirectoryModel>())
    , processingWatcher_(new QFutureWatcher<core::processing::ProcessingResult>(this))
    , orientationWatcher_(new QFutureWatcher<core::processing::ProcessingResult>(this))
    , channelWatcher_(new QFutureWatcher<core::processing::ChannelSplitResult>(this))
    , analysisWatcher_(new QFutureWatcher<core::analysis::AnalysisResult>(this))
    , preloadWatcher_(new QFutureWatcher<int>(this))
    , directoryWatcher_(new QFutureWatcher<core::navigation::DirectoryModel>(this))
    , saveWatcher_(new QFutureWatcher<AsyncSaveResult>(this))
    , processingTimer_(new QTimer(this))
{
    setWindowTitle(QString("ImageViewer"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    const QScreen* screen = QGuiApplication::primaryScreen();
    resize(screen ? QSize(1320, 860).boundedTo(screen->availableGeometry().size()) : QSize(1320, 860));
    setMinimumSize(720, 500);
    setAcceptDrops(true);

    setupUi();
    setupActions();
    setupMenusAndToolbar();
    setupStatusBar();
    connect(view_, &ui::ImageView::firstFramePainted, this, [this]() {
        if (bPerformanceTrace_ && requestTimer_.isValid() && !bLoading_) {
            qInfo() << "PERF first_frame_ms" << requestTimer_.elapsed()
                << "cache_hits" << imageCache_->hitCount() << "decodes" << imageCache_->decodeCount();
        }
    });

    processingTimer_->setSingleShot(true);
    processingTimer_->setInterval(80);
    connect(processingTimer_, &QTimer::timeout, this, &MainWindow::startPreprocess);
    connect(processingWatcher_, &QFutureWatcher<core::processing::ProcessingResult>::finished,
        this, &MainWindow::onPreprocessFinished);
    connect(orientationWatcher_, &QFutureWatcher<core::processing::ProcessingResult>::finished,
        this, &MainWindow::onOrientationFinished);
    connect(channelWatcher_, &QFutureWatcher<core::processing::ChannelSplitResult>::finished,
        this, &MainWindow::onChannelSplitFinished);
    connect(analysisWatcher_, &QFutureWatcher<core::analysis::AnalysisResult>::finished,
        this, &MainWindow::onRoiAnalysisFinished);
    connect(preloadWatcher_, &QFutureWatcher<int>::finished,
        this, &MainWindow::onNeighborPreloadFinished);
    connect(loadWatcher_, &QFutureWatcher<core::loader::LoadResult>::finished,
        this, &MainWindow::onImageLoadFinished);
    connect(directoryWatcher_, &QFutureWatcher<core::navigation::DirectoryModel>::finished,
        this, &MainWindow::onDirectoryScanFinished);
    connect(saveWatcher_, &QFutureWatcher<AsyncSaveResult>::finished, this, &MainWindow::onSaveFinished);
    QSettings settings;
    restoreGeometry(settings.value(QString("ui/geometry")).toByteArray());
    restoreState(settings.value(QString("ui/windowState")).toByteArray());
    analysisDock_->hide();
    bChannelSplitEnabled_ = actSplitChannels_->isChecked();
    updateChannelPanels();
    // 旧版 saveState 中工具栏默认可见，不覆盖本版独立的显隐设置。
    mainToolbar_->setVisible(actToggleToolbar_->isChecked());
    thumbnailContainer_->show();
    thumbnailBar_->setVisible(actToggleThumbnails_->isChecked());
    updateFullScreenLayout();
    view_->setSelectionEnabled(false);
    updateNavigationActions();
    updateResultActions();
    updateImageInformation();
}

MainWindow::~MainWindow()
{
    // 任务只持有值对象/共享缓存；销毁观察者不会让后台访问已释放的窗口。
    util::cancel(loadCanceled_);
    util::cancel(processingCanceled_);
    util::cancel(orientationCanceled_);
    util::cancel(channelCanceled_);
    util::cancel(analysisCanceled_);
    util::cancel(preloadCanceled_);
    util::cancel(directoryCanceled_);
}

void MainWindow::setupUi()
{
    // 单行顶栏：标题栏内嵌菜单按钮（QMenuBar 在无边框窗口中会被平台层当作拖拽区，
    // 导致菜单无法点击，因此改用 QToolButton + QMenu）。
    titleBar_ = new ui::TitleBar(this);
    titleBar_->addMenuButton(tr("文件(&F)"));
    titleBar_->addMenuButton(tr("视图(&V)"));
    titleBar_->addMenuButton(tr("语言(&L)"));
    titleBar_->addMenuButton(tr("设置(&S)"));
    titleBar_->addMenuButton(tr("帮助(&H)"));
    setMenuWidget(titleBar_);

    connect(titleBar_, &ui::TitleBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &ui::TitleBar::maximizeRestoreRequested, this, &MainWindow::toggleMaximized);
    connect(titleBar_, &ui::TitleBar::closeRequested, this, &QWidget::close);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    view_ = new ui::ImageView(central);
    channelLeftPanel_ = buildChannelPanel(central, false);
    channelRightPanel_ = buildChannelPanel(central, true);
    auto* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(channelLeftPanel_);
    contentLayout->addWidget(view_, 1);
    contentLayout->addWidget(channelRightPanel_);
    thumbnailBar_ = new ui::ThumbnailBar(central);
    centralLayout->addLayout(contentLayout, 1);
    thumbnailContainer_ = new QWidget(central);
    thumbnailContainer_->setObjectName(QString("ThumbnailContainer"));
    auto* thumbnailLayout = new QVBoxLayout(thumbnailContainer_);
    thumbnailLayout->setContentsMargins(0, 0, 0, 0);
    thumbnailLayout->setSpacing(0);
    auto* thumbnailHeader = new QWidget(thumbnailContainer_);
    thumbnailHeader->setObjectName(QString("ThumbnailHeader"));
    thumbnailHeader->setFixedHeight(28);
    auto* thumbnailHeaderLayout = new QHBoxLayout(thumbnailHeader);
    thumbnailHeaderLayout->setContentsMargins(10, 0, 6, 0);
    thumbnailHeaderLayout->setSpacing(4);
    auto* thumbnailTitle = new QLabel(tr("缩略图导航"), thumbnailHeader);
    thumbnailCollapseButton_ = new QToolButton(thumbnailHeader);
    thumbnailCollapseButton_->setObjectName(QString("CollapseThumbnailsButton"));
    thumbnailCollapseButton_->setArrowType(Qt::DownArrow);
    thumbnailCollapseButton_->setFixedSize(30, 24);
    thumbnailCollapseButton_->setFocusPolicy(Qt::NoFocus);
    thumbnailHeaderLayout->addWidget(thumbnailTitle);
    thumbnailHeaderLayout->addStretch();
    thumbnailHeaderLayout->addWidget(thumbnailCollapseButton_);
    thumbnailLayout->addWidget(thumbnailHeader);
    thumbnailLayout->addWidget(thumbnailBar_);
    centralLayout->addWidget(thumbnailContainer_);
    setCentralWidget(central);
    frameOverlay_ = new WindowFrameOverlay(this);
    frameOverlay_->setGeometry(rect());
    frameOverlay_->raise();

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
        "QWidget { font-family:'Microsoft YaHei UI'; font-size:13px; }"
        "QWidget#CustomTitleBar { background:#1d1f22; border-bottom:1px solid #30343a; }"
        "QLabel#TitleInfo { color:#dce1e7; font-size:13px; }"
        "QLabel#AppIcon { background:#1479ad; color:white; border-radius:4px; font-weight:600; }"
        "QWidget#CustomTitleBar QToolButton { border:0; background:transparent; color:#e7ebef; font-size:13px; }"
        "QWidget#CustomTitleBar QToolButton:hover { background:#33373d; }"
        "QWidget#CustomTitleBar QToolButton#CloseButton:hover { background:#c42b1c; }"
        "QWidget#CustomTitleBar QToolButton#MenuButton { border:0; background:transparent; color:#dfe3e8; font-size:13px; padding:4px 10px; border-radius:4px; }"
        "QWidget#CustomTitleBar QToolButton#MenuButton:hover { background:#33373d; color:white; }"
        "QWidget#CustomTitleBar QToolButton#MenuButton:pressed,"
        "QWidget#CustomTitleBar QToolButton#MenuButton:checked { background:#176b98; color:white; }"
        "QWidget#CustomTitleBar QToolButton::menu-indicator { image:none; width:0; }"
        "QMenu { background:#282b30; color:#e4e8ed; border:1px solid #454a52; padding:5px; }"
        "QMenu::item { padding:5px 24px 5px 10px; border-radius:4px; }"
        "QMenu::item:selected { background:#176b98; color:white; }"
        "QMenu::item:disabled { color:#747a83; }"
        "QMenu::separator { height:1px; background:#41464d; margin:5px 8px; }"
        "QToolBar { background:#222428; border:0; border-bottom:1px solid #34383e; spacing:3px; padding:4px 8px; }"
        "QToolButton { color:#dfe3e8; background:#30353b; border:1px solid #50565f; border-radius:4px; padding:4px 6px; }"
        "QToolButton:hover { background:#3c454e; }"
        "QToolBar QToolButton { color:#dfe3e8; background:transparent; border:1px solid transparent; border-radius:5px; min-height:26px; padding:4px 10px; }"
        "QToolBar QToolButton:hover { background:#343941; border-color:#454b54; color:white; }"
        "QToolBar QToolButton:pressed { background:#1c5e82; }"
        "QToolBar QToolButton:checked { background:#176b98; border-color:#268bc0; color:white; }"
        "QToolBar QToolButton:disabled { color:#666c74; background:transparent; }"
        "QToolBar::separator { width:1px; background:#434850; margin:5px 6px; }"
        "QStatusBar { background:#222428; color:#b9c0c8; border:0; min-height:26px; }"
        "QStatusBar::item { border:0; background:transparent; }"
        "QStatusBar QLabel { color:#b9c0c8; padding:0 8px; border:0; background:transparent; }"
        "QStatusBar QLabel#StatusPixel { color:#d5e3ed; }"
        "QDockWidget { background:#25282d; color:#e5e9ef; border-left:1px solid #3a3f46; }"
        "QDockWidget::title { background:#22252a; color:#e5e9ef; padding:9px 10px; text-align:left; border-bottom:1px solid #3b4047; }"
        "QDockWidget::close-button,QDockWidget::float-button { border:0; background:transparent; padding:4px; }"
        "QDockWidget::close-button:hover,QDockWidget::float-button:hover { background:#3a3f46; }"
        "QWidget#DockTitleBar { background:#22252a; border-bottom:1px solid #3b4047; }"
        "QLabel#DockTitleLabel { color:#e5e9ef; font-weight:600; }"
        "QToolButton#DockCloseButton { color:#c9cfd6; background:transparent; border:0; border-radius:4px; font-size:12px; }"
        "QToolButton#DockCloseButton:hover { color:white; background:#c42b1c; }"
        "QWidget#PreprocessPanel,QWidget#AnalysisPanel,QWidget#AnalysisContent { background:#25282d; color:#e1e5ea; }"
        "QTabWidget::pane { border:0; background:#25282d; }"
        "QTabBar::tab { background:#30343a; color:#dfe3e8; padding:6px 12px; }"
        "QTabBar::tab:selected { background:#176b98; color:white; }"
        "QFrame#ProcessingSwitchCard { background:#2b3b46; border:1px solid #31586f; border-radius:7px; }"
        "QCheckBox#ProcessingSwitch { color:#edf4f8; font-weight:600; spacing:9px; }"
        "QWidget#PreprocessParameters { background:transparent; }"
        "QScrollArea#PreprocessScroll { background:transparent; border:0; }"
        "QWidget#PreprocessPanel QGroupBox,QWidget#AnalysisPanel QGroupBox { background:#2b2e33; border:1px solid #41464e; border-radius:7px; margin-top:13px; padding-top:7px; font-weight:600; }"
        "QWidget#PreprocessPanel QGroupBox::title,QWidget#AnalysisPanel QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 6px; color:#cfd5dc; background:#2b2e33; }"
        "QWidget#PreprocessPanel QLabel,QWidget#AnalysisPanel QLabel { color:#cbd1d8; font-weight:400; }"
        "QWidget#PreprocessPanel QLabel#ValueBadge { color:#dceaf2; background:#202328; border:1px solid #444a52; border-radius:4px; padding:3px 4px; }"
        "QLineEdit,QComboBox,QSpinBox { color:#e4e8ed; background:#34383e; border:1px solid #50565f; border-radius:4px; padding:4px 8px; selection-background-color:#176b98; }"
        "QLineEdit:hover,QComboBox:hover,QSpinBox:hover { border-color:#6b747f; }"
        "QLineEdit:focus,QComboBox:focus,QSpinBox:focus { border-color:#2d9bd3; }"
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
        "QPushButton#AnalyzeFullImage, QDialog#AboutDialog QPushButton, QPushButton#SecondaryButton { color:#cfd5dc; background:#30343a; border:1px solid #50565f; border-radius:5px; padding:6px 12px; }"
        "QPushButton#AnalyzeFullImage:hover, QDialog#AboutDialog QPushButton:hover, QPushButton#SecondaryButton:hover { color:white; background:#393e45; border-color:#69727d; }"
        "QWidget#ChannelPanelLeft { background:#1c1f24; border-right:1px solid #3b434c; }"
        "QWidget#ChannelPanelRight { background:#1c1f24; border-left:1px solid #3b434c; }"
        "QWidget#ChannelPanelHeader { background:#23282e; border-bottom:1px solid #3b434c; }"
        "QLabel#ChannelPanelTitle { color:#e6edf4; font-weight:600; }"
        "QLabel#ChannelPanelRange { color:#8ea0af; font-size:12px; }"
        "QWidget#ChannelCards { background:#1c1f24; }"
        "QFrame#ChannelCard { background:#23282e; border:1px solid #3b444e; border-radius:7px; }"
        "QLabel#ChannelCardTitle { color:#e8eef4; font-weight:600; }"
        "QLabel#ChannelCardRange { color:#8ea0af; font-size:11px; }"
        "QLabel#ChannelThumbnail { color:#8d9aa6; background:#14171a; border:1px solid #2c343c; border-radius:5px; }"
        "QWidget#ThumbnailHeader { background:#292d32; border-top:1px solid #505963; }"
        "QWidget#ThumbnailHeader QLabel { color:#cbd3da; }"
        "QWidget#ThumbnailHeader QToolButton { background:transparent; border:0; padding:2px; }"
        "QWidget#ThumbnailHeader QToolButton:hover { background:#3c454e; }"
        "QListView#ThumbnailBar { background:#222428; color:#d7dce2; border:0; border-top:1px solid #34383e; padding:6px; outline:0; }"
        "QListView#ThumbnailBar::item { background:#292c31; color:#c8ced5; border:2px solid transparent; border-radius:6px; padding:3px; }"
        "QListView#ThumbnailBar::item:hover { background:#31363d; border-color:#4d555f; }"
        "QListView#ThumbnailBar::item:selected { background:#263f50; color:white; border-color:#2d9bd3; }"
        "QAbstractScrollArea::corner { background:#24272b; border:0; }"
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
                analysisPanel_->setPixel(position, color, bValid,
                    view_->pixelUsesOriginal(position) ? tr("原图") : tr("处理结果"));
            }
        });
    connect(view_, &ui::ImageView::roiSelected, this, &MainWindow::startRoiAnalysis);
    connect(analysisPanel_, &ui::AnalysisPanel::analyzeFullImageRequested, this, [this]() {
        view_->clearSelection();
        startRoiAnalysis(displayedImage_.rect());
    });
    connect(analysisPanel_, &ui::AnalysisPanel::analysisSourceChanged, this, &MainWindow::clearAnalysis);
    connect(view_, &ui::ImageView::fileDropped, this, [this](const QString& path) { openFiles({ path }); });
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
        view_->setSelectionEnabled(bVisible);
        if (bVisible) { preprocessDock_->hide(); }
        else { view_->clearSelection(); clearAnalysis(); }
    });
    connect(view_, &ui::ImageView::selectionCleared, this, &MainWindow::clearAnalysis);
    view_->setToolTip(tr("滚轮缩放；双击切换适配 / 100%；显示分析面板后 Shift + 拖动选择 ROI；Esc 清除选区或退出全屏；右键打开操作菜单"));
}

void MainWindow::setupActions()
{
    actNewWindow_ = new QAction(tr("新建窗口"), this);
    actNewWindow_->setObjectName(QString("NewWindow"));
    actNewWindow_->setShortcut(QKeySequence::New);
    actNewWindow_->setToolTip(tr("打开一个新的独立窗口，可同时查看不同文件夹的图像"));
    connect(actNewWindow_, &QAction::triggered, this, &MainWindow::onNewWindow);

    actOpen_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开"), this);
    actOpen_->setShortcut(QKeySequence::Open);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::onOpen);

    actOpenFolder_ = new QAction(tr("打开图像所在文件夹"), this);
    actOpenFolder_->setObjectName(QString("OpenContainingFolder"));
    actOpenFolder_->setEnabled(false);
    connect(actOpenFolder_, &QAction::triggered, this, &MainWindow::onOpenContainingFolder);

    actCopyFile_ = new QAction(tr("复制图像文件"), this);
    actCopyFile_->setObjectName(QString("CopyImageFile"));
    actCopyFile_->setEnabled(false);
    connect(actCopyFile_, &QAction::triggered, this, &MainWindow::onCopyImageFile);

    actFullScreen_ = new QAction(tr("全屏显示"), this);
    actFullScreen_->setObjectName(QString("ToggleFullScreen"));
    actFullScreen_->setCheckable(true);
    actFullScreen_->setShortcut(QKeySequence(Qt::Key_F11));
    actFullScreen_->setToolTip(tr("全屏后按 Esc 或 F11 退出"));
    connect(actFullScreen_, &QAction::toggled, this, [this](bool bFullScreen) {
        if (bFullScreen) {
            bWasMaximized_ = isMaximized();
            showFullScreen();
        } else {
            bWasMaximized_ ? showMaximized() : showNormal();
        }
        view_->setFocus();
    });

    actRotateLeft_ = new QAction(tr("向左旋转 90°"), this);
    actRotateLeft_->setObjectName(QString("RotateLeft"));
    actRotateLeft_->setShortcut(QKeySequence(QString("Ctrl+Shift+Left")));
    connect(actRotateLeft_, &QAction::triggered, this, [this]() {
        nQuarterTurns_ = (nQuarterTurns_ + 3) % 4;
        onOrientationChanged();
    });
    actRotateRight_ = new QAction(tr("向右旋转 90°"), this);
    actRotateRight_->setObjectName(QString("RotateRight"));
    actRotateRight_->setShortcut(QKeySequence(QString("Ctrl+Shift+Right")));
    connect(actRotateRight_, &QAction::triggered, this, [this]() {
        nQuarterTurns_ = (nQuarterTurns_ + 1) % 4;
        onOrientationChanged();
    });
    actMirrorHorizontal_ = new QAction(tr("水平镜像"), this);
    actMirrorHorizontal_->setObjectName(QString("MirrorHorizontal"));
    actMirrorHorizontal_->setCheckable(true);
    actMirrorHorizontal_->setShortcut(QKeySequence(QString("Ctrl+H")));
    connect(actMirrorHorizontal_, &QAction::triggered, this, [this]() {
        bMirrorHorizontal_ = actMirrorHorizontal_->isChecked();
        onOrientationChanged();
    });
    actMirrorVertical_ = new QAction(tr("垂直镜像"), this);
    actMirrorVertical_->setObjectName(QString("MirrorVertical"));
    actMirrorVertical_->setCheckable(true);
    actMirrorVertical_->setShortcut(QKeySequence(QString("Ctrl+J")));
    connect(actMirrorVertical_, &QAction::triggered, this, [this]() {
        bMirrorVertical_ = actMirrorVertical_->isChecked();
        onOrientationChanged();
    });
    actSplitChannels_ = new QAction(tr("拆分通道看图（RGB / HSV）"), this);
    actSplitChannels_->setObjectName(QString("SplitChannels"));
    actSplitChannels_->setCheckable(true);
    actSplitChannels_->setShortcut(QKeySequence(QString("Ctrl+Shift+C")));
    actSplitChannels_->setToolTip(tr("在主视图两侧显示 RGB / HSV 通道卡片；中间图像仍可缩放，设置会保留并跟随换图刷新"));
    actSplitChannels_->setChecked(QSettings().value(QString("ui/channelSplit"), false).toBool());
    connect(actSplitChannels_, &QAction::toggled, this, &MainWindow::setChannelSplitEnabled);

    actDeleteFile_ = new QAction(tr("删除"), this);
    actDeleteFile_->setObjectName(QString("DeleteImageFile"));
    actDeleteFile_->setShortcut(QKeySequence(Qt::Key_Delete));
    actDeleteFile_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actDeleteFile_->setEnabled(false);
    connect(actDeleteFile_, &QAction::triggered, this, &MainWindow::onDeleteImageFile);
    view_->addAction(actDeleteFile_);
    thumbnailBar_->addAction(actDeleteFile_);

    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(this);
        menu.addAction(actOpenFolder_);
        menu.addAction(actCopyFile_);
        menu.addSeparator();
        menu.addAction(actRotateLeft_);
        menu.addAction(actRotateRight_);
        menu.addAction(actMirrorHorizontal_);
        menu.addAction(actMirrorVertical_);
        menu.addAction(actSplitChannels_);
        menu.addSeparator();
        menu.addAction(actFullScreen_);
        menu.addSeparator();
        menu.addAction(actDeleteFile_);
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
    actPrevious_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view_->addAction(actPrevious_);
    thumbnailBar_->addAction(actPrevious_);

    actNext_ = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("下一张"), this);
    actNext_->setShortcut(QKeySequence(Qt::Key_Right));
    connect(actNext_, &QAction::triggered, this, &MainWindow::onNext);
    actNext_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view_->addAction(actNext_);
    thumbnailBar_->addAction(actNext_);
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
    actToggleThumbnails_ = new QAction(tr("显示缩略图"), this);
    actToggleThumbnails_->setObjectName(QString("ToggleThumbnails"));
    actToggleThumbnails_->setCheckable(true);
    actToggleThumbnails_->setShortcut(QKeySequence(QString("Ctrl+T")));
    connect(actToggleThumbnails_, &QAction::toggled, this, [this](bool bVisible) {
        // 标题栏常驻，只收起缩略图列表，保证折叠后仍有固定入口。
        if (thumbnailContainer_) { thumbnailContainer_->setVisible(!isFullScreen()); }
        if (thumbnailBar_) { thumbnailBar_->setVisible(bVisible && !isFullScreen()); }
        if (thumbnailCollapseButton_) {
            thumbnailCollapseButton_->setArrowType(bVisible ? Qt::DownArrow : Qt::RightArrow);
        }
        QSettings().setValue(QString("ui/thumbnailsVisible"), bVisible);
        view_->setFocus();
    });
    const bool bThumbnails = QSettings().value(QString("ui/thumbnailsVisible"), true).toBool();
    actToggleThumbnails_->setChecked(bThumbnails);
    thumbnailBar_->setVisible(bThumbnails);
    thumbnailCollapseButton_->setArrowType(bThumbnails ? Qt::DownArrow : Qt::RightArrow);
    thumbnailCollapseButton_->setDefaultAction(actToggleThumbnails_);
    thumbnailCollapseButton_->setToolTip(tr("折叠缩略图（Ctrl+T）"));
    connect(actToggleThumbnails_, &QAction::toggled, this, [this](bool bVisible) {
        thumbnailCollapseButton_->setToolTip(bVisible
            ? tr("折叠缩略图（Ctrl+T）") : tr("展开缩略图（Ctrl+T）"));
    });
    actLoupe_ = new QAction(tr("局部放大图"), this);
    actLoupe_->setObjectName(QString("ToggleLoupe"));
    actLoupe_->setCheckable(true);
    actLoupe_->setShortcut(QKeySequence(QString("Ctrl+M")));
    connect(actLoupe_, &QAction::toggled, this, [this](bool bEnabled) {
        view_->setLoupeEnabled(bEnabled);
        QSettings().setValue(QString("ui/loupeEnabled"), bEnabled);
    });
    const bool bLoupe = QSettings().value(QString("ui/loupeEnabled"), false).toBool();
    actLoupe_->setChecked(bLoupe);
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
    // 菜单挂在标题栏的按钮上，按钮由 setupUi 创建。
    auto* fileMenu = new QMenu(this);
    fileMenu->addAction(actNewWindow_);
    fileMenu->addAction(actOpen_);
    fileMenu->addAction(actRefresh_);
    fileMenu->addAction(actSaveResult_);
    fileMenu->addSeparator();
    fileMenu->addAction(actSavePreset_);
    fileMenu->addAction(actLoadPreset_);
    fileMenu->addSeparator();
    fileMenu->addAction(actExit_);
    if (auto* button = titleBar_->menuButton(0)) { button->setMenu(fileMenu); }
    auto* viewMenu = new QMenu(this);
    viewMenu->addAction(actFit_);
    viewMenu->addAction(actFitWidth_);
    viewMenu->addAction(actFitHeight_);
    viewMenu->addAction(actActualSize_);
    viewMenu->addSeparator();
    viewMenu->addAction(actRotateLeft_);
    viewMenu->addAction(actRotateRight_);
    viewMenu->addAction(actMirrorHorizontal_);
    viewMenu->addAction(actMirrorVertical_);
    viewMenu->addAction(actSplitChannels_);
    viewMenu->addSeparator();
    viewMenu->addAction(actTogglePreprocess_);
    viewMenu->addAction(actToggleAnalysis_);
    viewMenu->addAction(actCompare_);
    viewMenu->addSeparator();
    actToggleToolbar_ = new QAction(tr("显示工具栏"), this);
    actToggleToolbar_->setObjectName(QString("ToggleToolbar"));
    actToggleToolbar_->setCheckable(true);
    actToggleToolbar_->setShortcut(QKeySequence(QString("Ctrl+Shift+T")));
    actToggleToolbar_->setChecked(QSettings().value(QString("ui/toolbarVisible"), false).toBool());
    connect(actToggleToolbar_, &QAction::toggled, this, [this](bool bVisible) {
        if (mainToolbar_) { mainToolbar_->setVisible(bVisible && !isFullScreen()); }
        QSettings().setValue(QString("ui/toolbarVisible"), bVisible);
        view_->setFocus();
    });
    viewMenu->addAction(actToggleToolbar_);
    viewMenu->addAction(actToggleThumbnails_);
    viewMenu->addAction(actFullScreen_);
    viewMenu->addAction(actLoupe_);
    if (auto* button = titleBar_->menuButton(1)) { button->setMenu(viewMenu); }
    auto* languageMenu = new QMenu(this);
    QAction* chineseAction = languageMenu->addAction(tr("简体中文"));
    QAction* englishAction = languageMenu->addAction(QString("English"));
    connect(chineseAction, &QAction::triggered, this, [this]() { selectLanguage(QString("zh_CN")); });
    connect(englishAction, &QAction::triggered, this, [this]() { selectLanguage(QString("en_US")); });
    if (auto* button = titleBar_->menuButton(2)) { button->setMenu(languageMenu); }
    auto* settingsMenu = new QMenu(this);
    settingsMenu->addAction(actShowConsole_);
    if (auto* button = titleBar_->menuButton(3)) { button->setMenu(settingsMenu); }
    auto* helpMenu = new QMenu(this);
    helpMenu->addAction(actAbout_);
    if (auto* button = titleBar_->menuButton(4)) { button->setMenu(helpMenu); }

    auto* toolbar = addToolBar(tr("主工具栏"));
    mainToolbar_ = toolbar;
    toolbar->setObjectName(QString("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(actOpen_);
    toolbar->addAction(actSaveResult_);
    toolbar->addSeparator();
    toolbar->addAction(actFit_);
    toolbar->addAction(actActualSize_);
    toolbar->addSeparator();
    toolbar->addAction(actRotateLeft_);
    toolbar->addAction(actRotateRight_);
    toolbar->addAction(actMirrorHorizontal_);
    toolbar->addAction(actMirrorVertical_);
    toolbar->addAction(actSplitChannels_);
    toolbar->addSeparator();
    toolbar->addAction(actTogglePreprocess_);
    toolbar->addAction(actToggleAnalysis_);
    toolbar->addAction(actCompare_);
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    auto* collapse = new QToolButton(toolbar);
    collapse->setObjectName(QString("CollapseToolbarButton"));
    collapse->setDefaultAction(actToggleToolbar_);
    collapse->setToolButtonStyle(Qt::ToolButtonIconOnly);
    collapse->setArrowType(Qt::UpArrow);
    collapse->setFocusPolicy(Qt::NoFocus);
    collapse->setToolTip(tr("隐藏工具栏"));
    toolbar->addWidget(collapse);
    toolbar->setVisible(actToggleToolbar_->isChecked());
    view_->setChromeActions(actToggleToolbar_);
    // 菜单托管于标题栏按钮；显式注册快捷键，隐藏工具栏后仍可使用。
    addActions({ actOpen_, actSaveResult_, actFit_, actActualSize_, actFullScreen_,
        actRotateLeft_, actRotateRight_, actMirrorHorizontal_, actMirrorVertical_, actSplitChannels_,
        actToggleToolbar_, actToggleThumbnails_, actTogglePreprocess_, actToggleAnalysis_, actLoupe_, actCompare_ });
}

void MainWindow::updateFullScreenLayout()
{
    if (!mainToolbar_ || !actToggleToolbar_ || bFullScreenLayout_ == isFullScreen()) { return; }
    bFullScreenLayout_ = isFullScreen();
    if (bFullScreenLayout_) {
        bRestorePreprocess_ = !preprocessDock_->isHidden();
        bRestoreAnalysis_ = !analysisDock_->isHidden();
        mainToolbar_->hide();
        thumbnailContainer_->hide();
        preprocessDock_->hide();
        analysisDock_->hide();
    } else {
        mainToolbar_->setVisible(actToggleToolbar_->isChecked());
        thumbnailContainer_->show();
        thumbnailBar_->setVisible(actToggleThumbnails_->isChecked());
        preprocessDock_->setVisible(bRestorePreprocess_);
        analysisDock_->setVisible(bRestoreAnalysis_);
    }
    if (frameOverlay_ && !isMaximized() && !isFullScreen()) { frameOverlay_->raise(); }
    for (auto* action : { actToggleToolbar_, actToggleThumbnails_, actTogglePreprocess_, actToggleAnalysis_, actLoupe_ }) {
        action->setEnabled(!bFullScreenLayout_);
    }
    if (actSplitChannels_) {
        actSplitChannels_->setEnabled(!bFullScreenLayout_ && !displayedImage_.isNull()
            && !bLoading_ && !bOrienting_);
    }
    updateChannelPanels();
    view_->setLoupeEnabled(!bFullScreenLayout_ && actLoupe_->isChecked());
    view_->setMinimalMode(bFullScreenLayout_);
}

void MainWindow::setupStatusBar()
{
    statusFile_ = new QLabel(this);
    statusFile_->setObjectName(QString("StatusFile"));
    statusFile_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusFile_->setMinimumWidth(0);
    statusPixel_ = new QLabel(this);
    statusPixel_->setObjectName(QString("StatusPixel"));
    statusPixel_->setTextFormat(Qt::RichText);
    statusPixel_->ensurePolished();
    statusPixel_->setMinimumWidth(statusPixel_->fontMetrics().horizontalAdvance(
        QString("X: 00000  Y: 00000   R:255 G:255 B:255 A:255")) + 16);
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
        ? QString("X: %1  Y: %2   <span style=\"color:#ff6464\">R:%3</span> "
            "<span style=\"color:#5ad782\">G:%4</span> "
            "<span style=\"color:#64a5ff\">B:%5</span> A:%6")
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

void MainWindow::onNewWindow()
{
    // 以独立模式再启动一个进程，不转发给当前实例，可查看其他文件夹。
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
        { QString("--new-window") })) {
        QMessageBox::warning(this, tr("启动失败"), tr("无法启动新窗口进程。"));
    }
}

void MainWindow::onCopyImageFile()
{
    if (currentPath_.isEmpty()) {
        return;
    }
    // 以文件 URL 写入剪贴板，资源管理器中可直接粘贴复制该文件。
    auto* mimeData = new QMimeData();
    mimeData->setUrls({ QUrl::fromLocalFile(currentPath_) });
    QApplication::clipboard()->setMimeData(mimeData);
    statusBar()->showMessage(tr("已复制图像文件：%1").arg(QFileInfo(currentPath_).fileName()), 4000);
}

void MainWindow::onDeleteImageFile()
{
    if (currentPath_.isEmpty() || bLoading_) {
        return;
    }
    const QString path = currentPath_;
    if (QMessageBox::question(this, tr("删除图像"),
        tr("确定要删除该图像文件吗？\n%1").arg(path)) != QMessageBox::Yes) {
        return;
    }
    bool bRemoved = false;
    bool bTrashed = false;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    bTrashed = QFile::moveToTrash(path);
    bRemoved = bTrashed;
#endif
    if (!bRemoved) {
        bRemoved = QFile::remove(path);
    }
    if (!bRemoved) {
        QMessageBox::warning(this, tr("删除失败"), tr("无法删除文件：%1").arg(path));
        return;
    }
    imageCache_->remove(path);
    bRefreshDirectoryOnLoad_ = true;
    // 优先切到相邻图像；applyLoadedImage 会重新扫描目录并移除已删文件。
    const int nIndex = directoryModel_->currentIndex();
    QString nextPath;
    if (nIndex >= 0) {
        const QStringList& files = directoryModel_->files();
        if (nIndex + 1 < files.size()) {
            nextPath = files.at(nIndex + 1);
        } else if (nIndex > 0) {
            nextPath = files.at(nIndex - 1);
        }
    }
    if (!nextPath.isEmpty()) {
        openFile(nextPath);
    } else {
        clearAnalysis();
        currentPath_.clear();
        rawOriginalImage_ = QImage();
        originalImage_ = QImage();
        processedImage_ = QImage();
        displayedImage_ = QImage();
        ++nProcessingGeneration_;
        ++nAnalysisGeneration_;
        bProcessingPending_ = false;
        actCompare_->setChecked(false);
        actCompare_->setEnabled(false);
        view_->setImage(QImage());
        util::cancel(channelCanceled_);
        ++nChannelGeneration_;
        bChannelPending_ = false;
        for (int nChannel = 0; nChannel < 6; ++nChannel) {
            channelImages_[nChannel] = QImage();
            if (auto* label = static_cast<ChannelThumbnailLabel*>(channelCardImages_[nChannel])) {
                label->setMessage(tr("等待计算…"));
            }
        }
        updateChannelPanels();
        *directoryModel_ = core::navigation::DirectoryModel();
        util::cancel(directoryCanceled_);
        ++nDirectoryGeneration_;
        thumbnailBar_->setFiles(QStringList(), -1);
        statusProcessing_->clear();
        updateNavigationActions();
        updateImageInformation();
        updateResultActions();
    }
    statusBar()->showMessage((bTrashed ? tr("已移入回收站：%1") : tr("已删除：%1")).arg(path), 5000);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateWindowShape();
}

void MainWindow::updateWindowShape()
{
    const bool bFramed = !isMaximized() && !isFullScreen();
    if (frameOverlay_) {
        frameOverlay_->setGeometry(rect());
        frameOverlay_->setActive(bFramed);
        frameOverlay_->setVisible(bFramed);
        if (bFramed) { frameOverlay_->raise(); }
    }
    if (!bFramed) {
        clearMask();
        return;
    }
    // 圆角遮罩半径与描边一致，描边层已内缩，不会被遮罩裁掉。
    QPainterPath outline;
    outline.addRoundedRect(QRectF(rect()), 12.0, 12.0);
    setMask(QRegion(outline.toFillPolygon().toPolygon()));
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    updateWindowShape();
}

bool MainWindow::event(QEvent* event)
{
    const bool bHandled = QMainWindow::event(event);
    // 布局变化（工具栏/面板/缩略图显隐）可能把新控件抬到描边层之上。
    if (frameOverlay_ && event->type() == QEvent::LayoutRequest) {
        frameOverlay_->setGeometry(rect());
        if (!isMaximized() && !isFullScreen()) { frameOverlay_->raise(); }
    }
    return bHandled;
}

void MainWindow::openFiles(const QStringList& paths)
{
    QStringList files;
    QSet<QString> seen;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        const QString absolute = info.absoluteFilePath();
        if (!seen.contains(absolute.toCaseFolded())) { seen.insert(absolute.toCaseFolded()); files.append(absolute); }
    }
    if (files.isEmpty()) { return; }
    openFile(files.first());
    pendingFileList_ = files;
}

void MainWindow::openFile(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    if (bPerformanceTrace_) { requestTimer_.start(); }
    pendingFileList_.clear();
    bReloadAfterDirectory_ = false;
    util::cancel(loadCanceled_);
    util::cancel(preloadCanceled_);
    util::cancel(processingCanceled_);
    util::cancel(orientationCanceled_);
    util::cancel(channelCanceled_);
    ++nProcessingGeneration_;
    ++nOrientationGeneration_;
    ++nChannelGeneration_;
    bChannelPending_ = false;
    bProcessingPending_ = false;
    bOrienting_ = false;
    bOrientationPending_ = false;
    nQuarterTurns_ = 0;
    bMirrorHorizontal_ = false;
    bMirrorVertical_ = false;
    for (QAction* action : { actMirrorHorizontal_, actMirrorVertical_ }) {
        const QSignalBlocker blocker(action);
        action->setChecked(false);
    }
    processingTimer_->stop();
    util::cancel(directoryCanceled_);
    ++nDirectoryGeneration_;
    bDirectoryPending_ = false;
    clearAnalysis();
    requestedPath_ = QFileInfo(path).absoluteFilePath();
    ++nLoadGeneration_;
    bLoading_ = true;
    statusBar()->showMessage(tr("正在打开：%1").arg(QFileInfo(path).fileName()));
    updateResultActions();
    updateNavigationActions();
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
    loadCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = loadCanceled_;
    const auto cache = imageCache_;
    loadWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Decode), [path, cache, canceled]() {
        auto result = cache->load(path, canceled);
        const QFileInfo info(path);
        result.nFileBytes = info.size();
        result.modified = info.lastModified();
        return result;
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
        if (!currentPath_.isEmpty() && directoryModel_->count() == 0) { startDirectoryScan(); }
        updateNavigationActions();
        statusBar()->showMessage(tr("打开失败：%1").arg(result.error), 10000);
        updateResultActions();
        return;
    }
    if (bPerformanceTrace_) { qInfo() << "PERF decode_ready_ms" << requestTimer_.elapsed(); }
    nFileBytes_ = result.nFileBytes;
    fileModified_ = result.modified;
    statusBar()->clearMessage();
    applyLoadedImage(runningLoadPath_, result.image);
}

void MainWindow::applyLoadedImage(const QString& path, const QImage& image)
{
    const QStringList explicitFiles = pendingFileList_;
    pendingFileList_.clear();
    const bool bKnownPath = explicitFiles.isEmpty() && directoryModel_->setCurrentPath(path);
    if (!bKnownPath) {
        *directoryModel_ = core::navigation::DirectoryModel();
        thumbnailBar_->setFiles({}, -1);
    }
    currentPath_ = QFileInfo(path).absoluteFilePath();
    rawOriginalImage_ = image;
    originalImage_ = rawOriginalImage_;
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
    updateChannelPanels();
    if (bChannelSplitEnabled_) {
        refreshChannelSplit();
    }
    view_->setFocus();
    statusProcessing_->clear();
    QSettings().setValue(QString("files/lastDirectory"), QFileInfo(path).absolutePath());

    thumbnailBar_->setCurrentFileIndex(directoryModel_->currentIndex());
    if (processingParameters_.bEnabled) {
        bProcessingPending_ = true;
        statusProcessing_->setText(tr("等待预处理…"));
        processingTimer_->start();
    }
    updateNavigationActions();
    updateImageInformation();
    updateResultActions();
    if (!bKnownPath || bRefreshDirectoryOnLoad_) {
        scanFiles_ = explicitFiles;
        startDirectoryScan(bRefreshDirectoryOnLoad_);
        bRefreshDirectoryOnLoad_ = false;
    } else {
        scheduleNeighborPreload();
    }
}

void MainWindow::startDirectoryScan(bool bRefresh)
{
    util::cancel(directoryCanceled_);
    ++nDirectoryGeneration_;
    bDirectoryRefresh_ = bDirectoryRefresh_ || bRefresh;
    if (directoryWatcher_->isRunning()) { bDirectoryPending_ = true; return; }
    bDirectoryPending_ = false;
    nRunningDirectoryGeneration_ = nDirectoryGeneration_;
    scanPath_ = currentPath_;
    const QString path = scanPath_;
    const QStringList files = scanFiles_;
    scanFiles_.clear();
    const bool bForce = bDirectoryRefresh_;
    bDirectoryRefresh_ = false;
    directoryCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = directoryCanceled_;
    auto model = *directoryModel_;
    directoryWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Directory),
        [model, path, files, bForce, canceled]() mutable {
            if (!files.isEmpty()) { model.setFileList(files, path, canceled); }
            else { model.loadForFile(path, bForce, canceled); }
            return model;
        }));
}

void MainWindow::onDirectoryScanFinished()
{
    if (nRunningDirectoryGeneration_ == nDirectoryGeneration_ && scanPath_ == currentPath_) {
        if (bPerformanceTrace_) { qInfo() << "PERF directory_ready_ms" << requestTimer_.elapsed(); }
        const auto result = directoryWatcher_->result();
        const bool bChanged = result.files() != directoryModel_->files();
        *directoryModel_ = result;
        if (bChanged) { thumbnailBar_->setFiles(result.files(), result.currentIndex()); }
        else { thumbnailBar_->setCurrentFileIndex(result.currentIndex()); }
        updateNavigationActions();
        updateImageInformation();
        if (bReloadAfterDirectory_) {
            bReloadAfterDirectory_ = false;
            const QString path = result.currentIndex() < 0 && result.count() > 0
                ? result.files().at(std::clamp(nRefreshIndex_, 0, result.count() - 1)) : currentPath_;
            openFile(path);
        } else { scheduleNeighborPreload(); }
    }
    if (bDirectoryPending_) { startDirectoryScan(); }
}

void MainWindow::onOpen()
{
    const QString filter = tr("图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif);;所有文件 (*.*)");
    const QString path = QFileDialog::getOpenFileName(this, tr("打开图片"),
        QSettings().value(QString("files/lastDirectory")).toString(), filter);
    if (!path.isEmpty()) {
        openFiles({ path });
    }
}

void MainWindow::onSaveResult()
{
    if (!actSaveResult_->isEnabled() || displayedImage_.isNull()) {
        return;
    }
    const QString suggestedName = currentPath_.isEmpty()
        ? QString("result.png")
        : QFileInfo(currentPath_).completeBaseName() + (processedImage_.isNull() ? QString("_original.png") : QString("_processed.png"));
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
    saveResultTo(path);
}

bool MainWindow::saveResultTo(const QString& path)
{
    if (path.isEmpty() || !actSaveResult_->isEnabled() || displayedImage_.isNull() || bSaving_) { return false; }
    bSaving_ = true;
    savedPath_ = path;
    const QImage image = displayedImage_;
    const auto cache = imageCache_;
    statusBar()->showMessage(tr("正在保存：%1").arg(path));
    saveWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Save), [path, image, cache]() {
        AsyncSaveResult result;
        if (core::loader::saveImage(path, image, &result.error)) {
            cache->remove(path);
            const QFileInfo info(path);
            result.nFileBytes = info.size();
            result.modified = info.lastModified();
        }
        return result;
    }));
    updateResultActions();
    return true;
}

void MainWindow::onSaveFinished()
{
    const auto result = saveWatcher_->result();
    const QString error = result.error;
    bSaving_ = false;
    updateResultActions();
    if (!error.isEmpty()) {
        bCloseAfterSave_ = false;
        statusBar()->showMessage(tr("保存失败：%1").arg(error), 10000);
        return;
    }
    if (savedPath_ == currentPath_) {
        nFileBytes_ = result.nFileBytes;
        fileModified_ = result.modified;
        updateImageInformation();
    }
    thumbnailBar_->invalidateFile(savedPath_);
    if (!currentPath_.isEmpty() && QFileInfo(savedPath_).absolutePath() == QFileInfo(currentPath_).absolutePath()) {
        startDirectoryScan(true);
    }
    statusBar()->showMessage(tr("已保存：%1").arg(savedPath_), 5000);
    if (bCloseAfterSave_) { close(); }
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
    const int nRequestedIndex = bLoading_ ? directoryModel_->indexOf(requestedPath_) : -1;
    const int nIndex = nRequestedIndex >= 0 ? nRequestedIndex : directoryModel_->currentIndex();
    if (nIndex > 0) {
        openFile(directoryModel_->files().at(nIndex - 1));
    }
}

void MainWindow::onNext()
{
    const int nRequestedIndex = bLoading_ ? directoryModel_->indexOf(requestedPath_) : -1;
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
    imageCache_->remove(currentPath_);
    thumbnailBar_->invalidateFile(currentPath_);
    nRefreshIndex_ = directoryModel_->currentIndex();
    bReloadAfterDirectory_ = true;
    startDirectoryScan(true);
}

void MainWindow::onAbout()
{
    QDialog dialog(this);
    dialog.setObjectName(QString("AboutDialog"));
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setWindowTitle(tr("关于 ImageViewer"));
    dialog.setStyleSheet(QString("QDialog#AboutDialog { background:#292c31; } QDialog#AboutDialog QLabel { color:#d7dce2; }"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    auto* icon = new QLabel(&dialog);
    icon->setPixmap(windowIcon().pixmap(64, 64));
    layout->addWidget(icon);
    auto* information = new QLabel(&dialog);
    information->setObjectName(QString("AboutInformation"));
    information->setWordWrap(true);
    information->setMaximumWidth(600);
    information->setTextFormat(Qt::RichText);
    information->setTextInteractionFlags(Qt::TextBrowserInteraction);
    information->setOpenExternalLinks(true);
    information->setText(tr("<h2>ImageViewer v%1</h2><p>轻量图像查看与分析工具</p>"
        "<p>作者：Blazar<br>邮箱：<a style=\"color:#70c8ff\" href=\"mailto:blazarlin@gmail.com\">blazarlin@gmail.com</a></p>"
        "<p>源代码采用 MIT 许可证；第三方组件遵循各自许可证。</p>")
        .arg(app::version()));
    layout->addWidget(information);
    auto* close = new QPushButton(tr("关闭"), &dialog);
    close->setDefault(true);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(close, 0, Qt::AlignRight);
    dialog.exec();
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
    util::cancel(processingCanceled_);
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
    clearAnalysis();
    actCompare_->setEnabled(false);
    statusProcessing_->setText(tr("等待预处理…"));
    processingTimer_->start();
    updateResultActions();
}

void MainWindow::startPreprocess()
{
    if (bLoading_) { bProcessingPending_ = true; return; }
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
    processingCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = processingCanceled_;
    processingWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Compute), [source, parameters, canceled]() {
        return core::processing::ImageProcessor::process(source, parameters, canceled);
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
            if (bChannelSplitEnabled_) { refreshChannelSplit(); }
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

void MainWindow::onOrientationChanged()
{
    if (rawOriginalImage_.isNull() || bLoading_) { return; }
    util::cancel(processingCanceled_);
    util::cancel(channelCanceled_);
    ++nProcessingGeneration_;
    bProcessingPending_ = false;
    processingTimer_->stop();
    clearAnalysis();
    actCompare_->setChecked(false);
    actCompare_->setEnabled(false);
    util::cancel(orientationCanceled_);
    ++nOrientationGeneration_;
    bOrienting_ = true;
    startOrientation();
    updateResultActions();
}

void MainWindow::startOrientation()
{
    if (rawOriginalImage_.isNull() || bLoading_) { return; }
    if (orientationWatcher_->isRunning()) {
        bOrientationPending_ = true;
        return;
    }
    bOrientationPending_ = false;
    bOrienting_ = true;
    nRunningOrientationGeneration_ = nOrientationGeneration_;
    const QImage source = rawOriginalImage_;
    const int nTurns = nQuarterTurns_;
    const bool bHorizontal = bMirrorHorizontal_;
    const bool bVertical = bMirrorVertical_;
    orientationCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = orientationCanceled_;
    statusProcessing_->setText(tr("正在调整方向…"));
    orientationWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Compute),
        [source, nTurns, bHorizontal, bVertical, canceled]() {
            return core::processing::ImageProcessor::orient(
                source, nTurns, bHorizontal, bVertical, canceled);
        }));
}

void MainWindow::onOrientationFinished()
{
    const auto result = orientationWatcher_->result();
    if (nRunningOrientationGeneration_ == nOrientationGeneration_) {
        bOrienting_ = false;
        if (result.ok()) {
            originalImage_ = result.image;
            processedImage_ = QImage();
            displayedImage_ = originalImage_;
            if (processingParameters_.bEnabled) {
                bProcessingPending_ = true;
                statusProcessing_->setText(tr("等待预处理…"));
                processingTimer_->start();
            } else {
                view_->setImage(originalImage_, false);
                statusProcessing_->clear();
            }
            if (bChannelSplitEnabled_) { refreshChannelSplit(); }
        } else {
            statusProcessing_->clear();
            if (!result.error.isEmpty()) {
                statusBar()->showMessage(tr("方向调整失败：%1").arg(result.error), 8000);
            }
        }
        updateChannelPanels();
        updateImageInformation();
    }
    if (bOrientationPending_) { startOrientation(); }
    updateResultActions();
}

QWidget* MainWindow::buildChannelPanel(QWidget* parent, bool bHsv)
{
    auto* panel = new QWidget(parent);
    panel->setObjectName(bHsv ? QString("ChannelPanelRight") : QString("ChannelPanelLeft"));
    panel->setFixedWidth(228);
    panel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(panel);
    header->setObjectName(QString("ChannelPanelHeader"));
    header->setFixedHeight(34);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 10, 0);
    headerLayout->setSpacing(6);
    auto* title = new QLabel(bHsv ? tr("HSV 通道") : tr("RGB 通道"), header);
    title->setObjectName(QString("ChannelPanelTitle"));
    auto* range = new QLabel(bHsv ? tr("0–179 / 0–255") : tr("0–255"), header);
    range->setObjectName(QString("ChannelPanelRange"));
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(range);
    layout->addWidget(header);

    auto* cards = new QWidget(panel);
    cards->setObjectName(QString("ChannelCards"));
    auto* cardLayout = new QVBoxLayout(cards);
    cardLayout->setContentsMargins(8, 8, 8, 8);
    cardLayout->setSpacing(8);
    static const char* accentColors[6] = {
        "#ef6b6b", "#68cf7a", "#6aa6ff", "#e8c06a", "#6fc3d4", "#c8d2dc"
    };
    channelTitles_[0] = tr("红色通道");
    channelTitles_[1] = tr("绿色通道");
    channelTitles_[2] = tr("蓝色通道");
    channelTitles_[3] = tr("色相");
    channelTitles_[4] = tr("饱和度");
    channelTitles_[5] = tr("明度");
    channelRanges_[0] = channelRanges_[1] = channelRanges_[2] = QString("0–255");
    channelRanges_[3] = QString("0–179");
    channelRanges_[4] = channelRanges_[5] = QString("0–255");
    const int nBase = bHsv ? 3 : 0;
    for (int nOffset = 0; nOffset < 3; ++nOffset) {
        const int nIndex = nBase + nOffset;
        auto* card = new QFrame(cards);
        card->setObjectName(QString("ChannelCard"));
        card->setProperty("channelIndex", nIndex);
        auto* cardBody = new QVBoxLayout(card);
        cardBody->setContentsMargins(7, 5, 7, 7);
        cardBody->setSpacing(4);
        auto* cardHeader = new QHBoxLayout();
        cardHeader->setSpacing(6);
        auto* cardTitle = new QLabel(card);
        cardTitle->setObjectName(QString("ChannelCardTitle"));
        cardTitle->setTextFormat(Qt::RichText);
        const QString letter = QString::fromLatin1("RGBHSV").mid(nIndex, 1);
        cardTitle->setText(QString("<span style=\"color:%1;font-weight:700\">%2</span>  %3")
            .arg(QString::fromLatin1(accentColors[nIndex]), letter, channelTitles_[nIndex]));
        auto* cardRange = new QLabel(channelRanges_[nIndex], card);
        cardRange->setObjectName(QString("ChannelCardRange"));
        cardHeader->addWidget(cardTitle);
        cardHeader->addStretch();
        cardHeader->addWidget(cardRange);
        auto* image = new ChannelThumbnailLabel(card);
        image->setObjectName(QString("ChannelThumbnail%1").arg(nIndex));
        image->setMessage(tr("等待计算…"));
        cardBody->addLayout(cardHeader);
        cardBody->addWidget(image, 1);
        cardLayout->addWidget(card, 1);
        channelCardImages_[nIndex] = image;
    }
    layout->addWidget(cards, 1);
    panel->hide();
    return panel;
}

void MainWindow::updateChannelPanels()
{
    const bool bVisible = bChannelSplitEnabled_ && !displayedImage_.isNull() && !isFullScreen();
    if (channelLeftPanel_) { channelLeftPanel_->setVisible(bVisible); }
    if (channelRightPanel_) { channelRightPanel_->setVisible(bVisible); }
    if (bVisible) { updateChannelSource(); }
}

void MainWindow::updateChannelSource()
{
    if (!channelLeftPanel_ || !channelRightPanel_) { return; }
    if (currentPath_.isEmpty() || displayedImage_.isNull()) {
        channelLeftPanel_->setToolTip(QString());
        channelRightPanel_->setToolTip(QString());
        return;
    }
    QString text = QFileInfo(currentPath_).fileName();
    if (!processedImage_.isNull()) { text += tr(" · 处理结果"); }
    if (nQuarterTurns_ != 0 || bMirrorHorizontal_ || bMirrorVertical_) { text += tr(" · 已调整方向"); }
    channelLeftPanel_->setToolTip(text);
    channelRightPanel_->setToolTip(text);
}

void MainWindow::setChannelSplitEnabled(bool bEnabled)
{
    bChannelSplitEnabled_ = bEnabled;
    QSettings().setValue(QString("ui/channelSplit"), bEnabled);
    if (!bEnabled) {
        util::cancel(channelCanceled_);
        ++nChannelGeneration_;
        bChannelPending_ = false;
    }
    updateChannelPanels();
    if (bEnabled && !displayedImage_.isNull()) {
        refreshChannelSplit();
    }
    view_->setFocus();
    updateResultActions();
}

void MainWindow::refreshChannelSplit()
{
    if (!bChannelSplitEnabled_ || displayedImage_.isNull() || bLoading_ || bOrienting_) { return; }
    ++nChannelGeneration_;
    util::cancel(channelCanceled_);
    if (channelWatcher_->isRunning()) {
        bChannelPending_ = true;
        return;
    }
    bChannelPending_ = false;
    nRunningChannelGeneration_ = nChannelGeneration_;
    updateChannelSource();
    for (int nIndex = 0; nIndex < 6; ++nIndex) {
        channelImages_[nIndex] = QImage();
        if (auto* label = static_cast<ChannelThumbnailLabel*>(channelCardImages_[nIndex])) {
            label->setMessage(tr("正在计算…"));
        }
    }
    const QImage source = displayedImage_;
    channelCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = channelCanceled_;
    channelWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Compute),
        [source, canceled]() {
            return core::processing::ImageProcessor::splitChannels(source, QSize(512, 384), canceled);
        }));
}

void MainWindow::onChannelSplitFinished()
{
    const auto result = channelWatcher_->result();
    if (nRunningChannelGeneration_ == nChannelGeneration_ && bChannelSplitEnabled_) {
        if (result.ok()) {
            for (int nIndex = 0; nIndex < 6; ++nIndex) {
                channelImages_[nIndex] = result.images[nIndex];
                if (auto* label = static_cast<ChannelThumbnailLabel*>(channelCardImages_[nIndex])) {
                    label->setImage(channelImages_[nIndex]);
                }
            }
        } else {
            for (auto* label : channelCardImages_) {
                if (auto* thumbnail = static_cast<ChannelThumbnailLabel*>(label)) {
                    thumbnail->setMessage(tr("计算失败"));
                }
            }
            if (!result.error.isEmpty()) { statusBar()->showMessage(result.error, 8000); }
        }
    }
    if (bChannelPending_) { refreshChannelSplit(); }
    updateResultActions();
}

void MainWindow::startRoiAnalysis(const QRect& region)
{
    if (!analysisDock_->isVisible() || displayedImage_.isNull() || region.isEmpty() || bLoading_
        || bProcessingPending_ || processingTimer_->isActive()
        || (processingParameters_.bEnabled && processingWatcher_->isRunning())) {
        return;
    }
    pendingAnalysisRegion_ = region.intersected(displayedImage_.rect());
    if (pendingAnalysisRegion_.isEmpty()) { return; }
    if (analysisCanceled_) { analysisCanceled_->store(true); }
    ++nAnalysisGeneration_;
    analysisPanel_->setSelection(pendingAnalysisRegion_);
    analysisPanel_->setBusy(true);
    if (analysisWatcher_->isRunning()) {
        bAnalysisPending_ = true;
        return;
    }
    bAnalysisPending_ = false;
    nRunningAnalysisGeneration_ = nAnalysisGeneration_;
    const QImage image = analysisPanel_->usesOriginal() ? originalImage_ : displayedImage_;
    const QRect requestedRegion = pendingAnalysisRegion_;
    analysisCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = analysisCanceled_;
    analysisWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Compute), [image, requestedRegion, canceled]() {
        return core::analysis::ImageAnalysis::analyze(image, requestedRegion, canceled);
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
    if (preloadWatcher_->isRunning()) { bPreloadPending_ = true; return; }
    bPreloadPending_ = false;
    const int nIndex = directoryModel_->currentIndex();
    if (nIndex < 0 || !core::cache::ImageCache::canPrefetch(originalImage_.size())) { return; }
    QStringList requests;
    for (int nCandidate : { nIndex + 1, nIndex - 1 }) {
        if (nCandidate >= 0 && nCandidate < directoryModel_->count()) { requests.append(directoryModel_->files().at(nCandidate)); }
    }
    const auto cache = imageCache_;
    preloadCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = preloadCanceled_;
    preloadWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Prefetch), [requests, cache, canceled]() {
        int nLoaded = 0;
        for (const QString& path : requests) {
            if (util::canceled(canceled)) { break; }
            QImageReader reader(path);
            if (!core::cache::ImageCache::canPrefetch(reader.size())) { continue; }
            // 每张完成即入共享缓存，结果数组不持有整批图片。
            if (cache->load(path, canceled).ok()) { ++nLoaded; }
        }
        return nLoaded;
    }));
}

void MainWindow::onNeighborPreloadFinished()
{
    if (bPreloadPending_ && !bLoading_) { scheduleNeighborPreload(); }
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
    updateChannelPanels();
    if (bChannelSplitEnabled_) { refreshChannelSplit(); }
    updateResultActions();
}

void MainWindow::clearAnalysis()
{
    ++nAnalysisGeneration_;
    if (analysisCanceled_) { analysisCanceled_->store(true); }
    bAnalysisPending_ = false;
    pendingAnalysisRegion_ = QRect();
    analysisPanel_->clear();
}

void MainWindow::updateResultActions()
{
    const bool bReady = !bLoading_ && !bOrienting_ && !displayedImage_.isNull()
        && (!processingParameters_.bEnabled || (!bProcessingPending_
            && !processingTimer_->isActive() && !processingWatcher_->isRunning()));
    analysisPanel_->setImageState(bReady, !processedImage_.isNull());
    actSaveResult_->setEnabled(bReady && !bSaving_);
    actCompare_->setEnabled(bReady && !processedImage_.isNull());
    actRefresh_->setEnabled(!currentPath_.isEmpty() && !bLoading_);
    actOpenFolder_->setEnabled(!currentPath_.isEmpty() && !bLoading_ && !bOrienting_);
    actCopyFile_->setEnabled(!currentPath_.isEmpty() && !bLoading_ && !bOrienting_);
    actDeleteFile_->setEnabled(!currentPath_.isEmpty() && !bLoading_ && !bOrienting_);
    for (QAction* action : { actRotateLeft_, actRotateRight_, actMirrorHorizontal_, actMirrorVertical_ }) {
        action->setEnabled(!bLoading_ && !bOrienting_ && !rawOriginalImage_.isNull());
    }
    // 通道计算期间仍允许关闭通道视图，否则用户无法退出等待状态。
    actSplitChannels_->setEnabled(!displayedImage_.isNull() && !bLoading_ && !bOrienting_);
    for (QAction* action : { actFit_, actFitWidth_, actFitHeight_, actActualSize_ }) {
        action->setEnabled(!displayedImage_.isNull() && !bOrienting_);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (bSaving_) {
        bCloseAfterSave_ = true;
        statusBar()->showMessage(tr("正在完成保存，完成后自动关闭"));
        event->ignore();
        return;
    }
    util::cancel(loadCanceled_);
    util::cancel(processingCanceled_);
    util::cancel(orientationCanceled_);
    util::cancel(channelCanceled_);
    util::cancel(analysisCanceled_);
    util::cancel(preloadCanceled_);
    util::cancel(directoryCanceled_);
    QSettings settings;
    settings.setValue(QString("ui/geometry"), saveGeometry());
    settings.setValue(QString("ui/windowState"), saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::updateNavigationActions()
{
    const int nRequested = bLoading_ ? directoryModel_->indexOf(requestedPath_) : -1;
    const int nIndex = nRequested >= 0 ? nRequested : directoryModel_->currentIndex();
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
    statusFile_->setToolTip(tr("%1\n修改时间：%2").arg(currentPath_,
        fileModified_.toString(QString("yyyy/MM/dd HH:mm"))));
    statusSize_->setText(QString("%1 × %2").arg(originalImage_.width()).arg(originalImage_.height())
        + (util::highDepth(originalImage_) ? tr(" | 取样/统计/处理：8 位") : QString()));
    titleBar_->setInfoText(formatTitleText(), currentPath_);
    setWindowTitle(QString("ImageViewer - %1").arg(QFileInfo(currentPath_).fileName()));
}

QString MainWindow::formatTitleText() const
{
    const int nIndex = directoryModel_->currentIndex();
    // 标题栏保持紧凑：版本号在“关于”中展示，避免长文本被窗口宽度裁切。
    const QString zoomText = QString("%1%").arg(QString::number(
        view_->zoomFactor() * 100.0, 'f', view_->zoomFactor() < 0.1 ? 1 : 0));
    return tr("%1  |  %2/%3  |  %4  |  %5×%6  |  %7  |  %8")
        .arg(QFileInfo(currentPath_).fileName())
        .arg(nIndex >= 0 ? nIndex + 1 : 0)
        .arg(directoryModel_->count())
        .arg(zoomText)
        .arg(originalImage_.width()).arg(originalImage_.height())
        .arg(formatFileSize(nFileBytes_))
        .arg(pixelFormatText(originalImage_));
}

QString MainWindow::pixelFormatText(const QImage& image) const
{
    const int nDepth = image.depth();
    if (util::grayFormat(image)) {
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
    if (isFullScreen()) { actFullScreen_->setChecked(false); return; }
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
            openFiles({ path });
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && titleBar_) {
        titleBar_->setMaximized(isMaximized() || isFullScreen());
        updateWindowShape();
        updateFullScreenLayout();
        if (actFullScreen_) {
            const QSignalBlocker blocker(actFullScreen_);
            actFullScreen_->setChecked(isFullScreen());
            actFullScreen_->setText(isFullScreen() ? tr("退出全屏") : tr("全屏显示"));
        }
    }
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, long* result)
{
    Q_UNUSED(eventType)
    MSG* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
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
