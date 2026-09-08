// 2026-09-06
// 功能：独立验证目录导航与 OpenCV 预处理核心行为。
// 目的：在不启动主窗口的情况下提供可重复的中文业务验收输出。
#include <Windows.h>
#include <QApplication>
#include <QAction>
#include <QBuffer>
#include <QDockWidget>
#include <QGroupBox>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QLineEdit>
#include <QProcess>
#include "app/Application.h"
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QComboBox>
#include <QTextDocument>
#include <QToolTip>
#include <QPushButton>
#include <QTimer>
#include <QDialog>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QLabel>
#include <functional>
#include "app/MainWindow.h"
#include "ui/ImageView.h"
#include "ui/AnalysisPanel.h"
#include "ui/PreprocessPanel.h"
#include "ui/ThumbnailBar.h"
#include "core/loader/ImageLoader.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTextCodec>

#include "core/navigation/DirectoryModel.h"
#include "core/analysis/ImageAnalysis.h"
#include "core/cache/ImageCache.h"
#include "core/cache/ThumbnailCache.h"
#include "core/cache/ImagePyramid.h"
#include "core/processing/ImageProcessor.h"
#include "core/processing/ProcessingPreset.h"

#include <opencv2/core/utils/logger.hpp>

#include <cmath>
#include <cstdio>

namespace {

int nFailedTests = 0;

void verify(bool bCondition, const QString& testName, const QString& goal)
{
    if (bCondition) {
        qInfo().noquote() << QString("[PASS] %1：%2").arg(testName, goal);
    } else {
        qCritical().noquote() << QString("[FAIL] %1：%2").arg(testName, goal);
        ++nFailedTests;
    }
}

void testDirectoryNavigation(const QString& outputRoot)
{
    const QString directoryPath = QDir(outputRoot).filePath(QString("directory_navigation"));
    QDir().mkpath(directoryPath);
    QImage(4, 4, QImage::Format_RGB32).save(QDir(directoryPath).filePath(QString("img2.png")));
    QImage(4, 4, QImage::Format_RGB32).save(QDir(directoryPath).filePath(QString("img10.png")));
    QFile textFile(QDir(directoryPath).filePath(QString("readme.txt")));
    textFile.open(QIODevice::WriteOnly);
    textFile.write("not an image");
    textFile.close();

    core::navigation::DirectoryModel model;
    const bool bLoaded = model.loadForFile(QDir(directoryPath).filePath(QString("img10.png")));
    const int nIndex2 = model.files().indexOf(QDir(directoryPath).filePath(QString("img2.png")));
    const int nIndex10 = model.files().indexOf(QDir(directoryPath).filePath(QString("img10.png")));
    verify(bLoaded && nIndex2 >= 0 && nIndex10 > nIndex2,
        QString("TEST-01"), QString("验证目录图像过滤与数字自然排序"));
}

void testProcessorPassThrough()
{
    QImage image(3, 2, QImage::Format_RGB888);
    image.fill(QColor(12, 34, 56));
    core::processing::ProcessingParameters parameters;
    const auto result = core::processing::ImageProcessor::process(image, parameters);
    verify(result.ok() && result.image == image,
        QString("TEST-02"), QString("验证未启用预处理时与原图完全一致"));
}

void testBrightnessSaturation()
{
    QImage image(1, 1, QImage::Format_Grayscale8);
    image.fill(250);
    core::processing::ProcessingParameters parameters;
    parameters.bEnabled = true;
    parameters.nBrightness = 20;
    const auto result = core::processing::ImageProcessor::process(image, parameters);
    verify(result.ok() && result.image.constBits()[0] == 255,
        QString("TEST-03"), QString("验证亮度调整使用 8 位饱和而不溢出"));
}

void testThresholdAndDimensions()
{
    QImage image(16, 8, QImage::Format_Grayscale8);
    for (int nY = 0; nY < image.height(); ++nY) {
        uchar* line = image.scanLine(nY);
        for (int nX = 0; nX < image.width(); ++nX) {
            line[nX] = nX < 8 ? 20 : 230;
        }
    }
    core::processing::ProcessingParameters parameters;
    parameters.bEnabled = true;
    parameters.threshold = core::processing::ThresholdMode::Otsu;
    parameters.smooth = core::processing::SmoothMode::Gaussian;
    parameters.nSmoothKernel = 4;
    const auto result = core::processing::ImageProcessor::process(image, parameters);
    bool bBinary = result.ok() && result.image.size() == image.size();
    if (bBinary) {
        for (int nY = 0; nY < result.image.height() && bBinary; ++nY) {
            const uchar* line = result.image.constScanLine(nY);
            for (int nX = 0; nX < result.image.width(); ++nX) {
                if (line[nX] != 0 && line[nX] != 255) {
                    bBinary = false;
                    break;
                }
            }
        }
    }
    verify(bBinary, QString("TEST-04"),
        QString("验证偶数滤波核归一化、Otsu 二值输出和尺寸不变"));
}

void testChannelSelection()
{
    QImage image(1, 1, QImage::Format_RGB888);
    image.setPixelColor(0, 0, QColor(210, 80, 30));
    core::processing::ProcessingParameters parameters;
    parameters.bEnabled = true;
    parameters.channel = core::processing::ChannelView::Red;
    const auto result = core::processing::ImageProcessor::process(image, parameters);
    verify(result.ok() && result.image.isGrayscale() && result.image.constBits()[0] == 210,
        QString("TEST-05"), QString("验证 RGB/BGR 转换后红通道值准确"));
}

void testRoiStatistics()
{
    QImage image(2, 1, QImage::Format_RGB888);
    image.setPixelColor(0, 0, QColor(10, 20, 30));
    image.setPixelColor(1, 0, QColor(30, 40, 50));
    const auto result = core::analysis::ImageAnalysis::analyze(image, image.rect());
    const bool bStatisticsCorrect = result.ok()
        && result.nPixelCount == 2
        && result.rgb[0].dMinimum == 10.0
        && result.rgb[0].dMaximum == 30.0
        && std::abs(result.rgb[0].dMean - 20.0) < 0.001
        && std::abs(result.rgb[0].dStandardDeviation - 10.0) < 0.001;
    quint64 nHistogramCount = 0;
    for (quint64 nValue : result.grayHistogram) {
        nHistogramCount += nValue;
    }
    verify(bStatisticsCorrect && nHistogramCount == 2,
        QString("TEST-06"), QString("验证 ROI 边界、RGB 统计与灰度直方图像素守恒"));
}

void testProcessingPreset()
{
    core::processing::ProcessingParameters source;
    source.bEnabled = true;
    source.nBrightness = 31;
    source.dGamma = 1.7;
    source.edge = core::processing::EdgeMode::Canny;
    source.nCannyLow = 180;
    source.nCannyHigh = 60;
    source.nSmoothKernel = 4;
    core::processing::ProcessingParameters loaded;
    QString error;
    const bool bLoaded = core::processing::ProcessingPreset::fromJson(
        core::processing::ProcessingPreset::toJson(source), &loaded, &error);
    verify(bLoaded && error.isEmpty() && loaded.bEnabled
            && loaded.nBrightness == 31 && std::abs(loaded.dGamma - 1.7) < 0.001
            && loaded.edge == core::processing::EdgeMode::Canny
            && loaded.nCannyLow == 60 && loaded.nCannyHigh == 180
            && loaded.nSmoothKernel == 5,
        QString("TEST-07"), QString("验证预处理预设 JSON 往返、奇数核归一化与 Canny 阈值排序"));
}

void testCacheInvalidation(const QString& outputRoot)
{
    const QString path = QDir(outputRoot).filePath(QString("cache_source.bin"));
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write("a");
    file.close();
    const QString imageKeyBefore = core::cache::ImageCache::keyForFile(path);
    const QString thumbnailKeyBefore = core::cache::ThumbnailCache::keyForFile(path, QSize(112, 76));
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.write("b");
    file.close();
    const QString imageKeyAfter = core::cache::ImageCache::keyForFile(path);
    const QString thumbnailKeyAfter = core::cache::ThumbnailCache::keyForFile(path, QSize(112, 76));
    verify(imageKeyBefore != imageKeyAfter && thumbnailKeyBefore != thumbnailKeyAfter,
        QString("TEST-08"), QString("验证源文件大小变化后解码缓存与缩略图缓存自动失效"));
}

void testImagePyramid()
{
    QImage image(4097, 2049, QImage::Format_Grayscale8);
    image.fill(128);
    const std::vector<QImage> levels = core::cache::ImagePyramid::build(image, 1024, 8);
    const bool bValid = levels.size() == 4
        && levels[0].size() == QSize(4097, 2049)
        && levels[1].size() == QSize(2049, 1025)
        && levels[2].size() == QSize(1025, 513)
        && levels[3].size() == QSize(513, 257);
    verify(bValid, QString("TEST-09"),
        QString("验证奇数尺寸大图金字塔逐级二分、层数受限且最终层不超过阈值"));
}

bool waitUntil(const std::function<bool()>& predicate, int nTimeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) { return true; }
        QThread::msleep(5);
    } while (timer.elapsed() < nTimeoutMs);
    return false;
}

void testRefreshAndAtomicSave(const QString& outputRoot)
{
    QTemporaryDir directory(QDir(outputRoot).filePath(QString("refresh-XXXXXX")));
    QImage image(12, 8, QImage::Format_RGB32);
    image.fill(QColor(20, 30, 40));
    const QString first = directory.filePath(QString("1.png"));
    image.save(first);
    core::navigation::DirectoryModel model;
    model.loadForFile(first);
    const QString second = directory.filePath(QString("2.png"));
    image.save(second);
    model.loadForFile(first, true);
    const bool bAdded = model.count() == 2;
    QFile::remove(second);
    model.loadForFile(first, true);
    const QString unknown = directory.filePath(QString("图片.data"));
    image.save(unknown, "PNG");
    const bool bUnknown = model.loadForFile(unknown) && model.currentPath() == unknown;
    verify(bAdded && model.count() == 2 && bUnknown, QString("TEST-10"),
        QString("刷新可见新增/删除图片，内容可解码的未知扩展名也进入导航"));

    image.save(second);
    QFile::remove(first);
    verify(model.loadForFile(first, true) && model.currentIndex() == -1
            && model.files().contains(second) && !model.files().contains(first),
        QString("TEST-23"), QString("当前文件被外部删除后仍能刷新目录，且不会保留错误索引"));

    const QString target = directory.filePath(QString("result.unsupported"));
    QFile file(target);
    file.open(QIODevice::WriteOnly);
    file.write("original");
    file.close();
    QString error;
    const bool bFailed = !core::loader::saveImage(target, image, &error);
    file.open(QIODevice::ReadOnly);
    const bool bPreserved = file.readAll() == QByteArray("original");
    file.close();
    const QString png = directory.filePath(QString("中文结果.png"));
    const bool bSaved = core::loader::saveImage(png, image, &error);
    verify(bFailed && bPreserved && bSaved && QImage(png) == image,
        QString("TEST-11"), QString("不支持编码时保留原文件，中文路径 PNG 无损往返"));
    verify(!core::loader::LoadResult().ok()
            && !core::loader::loadImage(target).ok()
            && core::loader::loadImage(unknown).ok(),
        QString("TEST-12"), QString("空结果与损坏图片不能误报成功，未知扩展名按内容加载"));
}

void testViewInteractions()
{
    ui::ImageView view;
    view.resize(500, 400);
    view.show();
    QImage image(100, 80, QImage::Format_RGB32);
    image.fill(Qt::red);
    view.setImage(image);
    QCoreApplication::processEvents();
    const double dFit = view.zoomFactor();
    const QPoint center = view.viewport()->rect().center();
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, center, Qt::LeftButton,
        Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &doubleClick);
    const bool bActual = std::abs(view.zoomFactor() - 1.0) < 0.001;
    QCoreApplication::sendEvent(view.viewport(), &doubleClick);
    verify(bActual && std::abs(view.zoomFactor() - dFit) < 0.001,
        QString("TEST-13"), QString("连续两次双击在适配和 100% 间往返"));
    view.actualSize();
    const QPoint first = view.mapFromScene(QPointF(10, 12));
    const QPoint last = view.mapFromScene(QPointF(30, 32));
    QMouseEvent press(QEvent::MouseButtonPress, first, Qt::LeftButton,
        Qt::LeftButton, Qt::ShiftModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, last, Qt::LeftButton,
        Qt::NoButton, Qt::ShiftModifier);
    QCoreApplication::sendEvent(view.viewport(), &press);
    QCoreApplication::sendEvent(view.viewport(), &release);
    const bool bSelection = view.selectedRegion() == QRect(10, 12, 20, 20);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &escape);
    verify(bSelection && view.selectedRegion().isEmpty(), QString("TEST-14"),
        QString("ROI 使用松手位置确定边界、松手后保留选框、Esc 可清除"));
}

void testExifThumbnail(const QString& outputRoot)
{
    QImage landscape(120, 60, QImage::Format_RGB32);
    landscape.fill(Qt::green);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    landscape.save(&buffer, "JPEG");
    buffer.close();
    // EXIF Orientation=6，显示时顺时针旋转 90 度。
    bytes.insert(2, QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000"));
    const QString path = QDir(outputRoot).filePath(QString("EXIF旋转.jpg"));
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(bytes);
    file.close();
    const auto loaded = core::loader::loadImage(path);
    ui::ThumbnailBar bar;
    bar.resize(400, 132);
    bar.show();
    bar.setFiles({ path }, 0);
    const bool bLoaded = waitUntil([&]() { return !bar.item(0)->icon().isNull(); });
    const QSize size = bar.item(0)->icon().actualSize(QSize(112, 76));
    verify(loaded.ok() && loaded.image.size() == QSize(60, 120)
            && bLoaded && size.height() > size.width(), QString("TEST-22"),
        QString("带 EXIF 旋转的 JPEG 主图与缩略图方向一致"));
}

void testWindowWorkflow(const QString& outputRoot)
{
    QTemporaryDir directory(QDir(outputRoot).filePath(QString("workflow-XXXXXX")));
    QImage first(160, 120, QImage::Format_RGB32);
    first.fill(QColor(20, 30, 40));
    QImage second(first.size(), first.format());
    second.fill(QColor(100, 110, 120));
    const QString firstPath = directory.filePath(QString("图片1.png"));
    const QString badPath = directory.filePath(QString("图片2.png"));
    const QString secondPath = directory.filePath(QString("图片3.bmp"));
    first.save(firstPath);
    second.save(secondPath);
    QFile bad(badPath);
    bad.open(QIODevice::WriteOnly);
    bad.write("broken");
    bad.close();
    MainWindow window;
    window.show();
    auto* view = window.findChild<ui::ImageView*>();
    auto* thumbnails = window.findChild<ui::ThumbnailBar*>();
    auto* preprocess = window.findChild<ui::PreprocessPanel*>();
    auto* dock = window.findChild<QDockWidget*>(QString("PreprocessDock"));
    auto* analysisDock = window.findChild<QDockWidget*>(QString("AnalysisDock"));
    QAction* save = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == QKeySequence(QString("Ctrl+Shift+S"))) { save = action; }
    }
    window.openFile(firstPath);
    const bool bDeferred = view->image().isNull();
    window.openFile(secondPath);
    verify(bDeferred && waitUntil([&]() { return view->image() == second; }),
        QString("TEST-15"), QString("打开异步返回，连续请求仅最后一张成为当前图"));
    window.openFile(firstPath);
    waitUntil([&]() { return view->image() == first; });
    QMetaObject::invokeMethod(&window, "onNext");
    const bool bFailed = waitUntil([&]() { return window.statusBar()->currentMessage().contains(QString("打开失败")); });
    verify(bFailed && thumbnails->currentRow() == 0 && view->image() == first,
        QString("TEST-16"), QString("下一张损坏时保留原图、当前索引和缩略图选中项"));

    dock->show();
    core::processing::ProcessingParameters parameters;
    parameters.bEnabled = true;
    parameters.nBrightness = 30;
    preprocess->setParameters(parameters);
    const bool bSaveBlocked = save && !save->isEnabled();
    waitUntil([&]() { return save && save->isEnabled(); });
    const QImage processed = view->image();
    analysisDock->show();
    dock->hide();
    QCoreApplication::processEvents();
    verify(bSaveBlocked && processed.pixelColor(0, 0).red() == 50
            && preprocess->parameters().bEnabled && view->image() == processed,
        QString("TEST-17"), QString("参数改变即禁用导出；切换到分析面板和隐藏预处理均保留结果"));

    QMetaObject::invokeMethod(&window, "startRoiAnalysis", Q_ARG(QRect, first.rect()));
    preprocess->setProcessingEnabled(false);
    waitUntil([&]() { return view->image() == first; });
    QElapsedTimer settle;
    settle.start();
    waitUntil([&]() { return settle.elapsed() > 150; });
    bool bOldStatistics = false;
    for (QLabel* label : analysisDock->findChildren<QLabel*>()) {
        bOldStatistics = bOldStatistics || label->text() == QString("完成");
    }
    verify(!bOldStatistics && view->selectedRegion().isEmpty(), QString("TEST-18"),
        QString("恢复原图后废弃正在计算的旧 ROI 结果"));

    QMimeData mime;
    mime.setUrls({ QUrl::fromLocalFile(secondPath) });
    const QPoint point = view->viewport()->rect().center();
    QDragEnterEvent drag(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &drag);
    QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &drop);
    verify(drag.isAccepted() && drop.isAccepted()
            && waitUntil([&]() { return view->image() == second; })
            && thumbnails->currentRow() == 2,
        QString("TEST-19"), QString("本地文件拖放事件通过主视图到主窗口，更新图片及同目录缩略图"));
    verify(waitUntil([&]() { return !thumbnails->item(2)->icon().isNull(); }),
        QString("TEST-20"), QString("当前图片缩略图在后台完成加载"));
    analysisDock->hide();
    view->actualSize();
    const QPoint pixelPoint = view->mapFromScene(QPointF(12, 18));
    QMouseEvent hover(QEvent::MouseMove, pixelPoint, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &hover);
    auto* pixelStatus = window.findChild<QLabel*>(QString("StatusPixel"));
    QTextDocument pixelText;
    pixelText.setHtml(pixelStatus ? pixelStatus->text() : QString());
    verify(pixelStatus && pixelText.toPlainText() == QString("X: 12 Y: 18 R:100 G:110 B:120 A:255")
            && pixelStatus->text().contains(QString("#ff6464"))
            && pixelStatus->text().contains(QString("#5ad782"))
            && pixelStatus->text().contains(QString("#64a5ff"))
            && !analysisDock->isVisible(), QString("TEST-24"),
        QString("分析面板隐藏时状态栏仍实时显示原始图像坐标和 RGBA 值"));
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(view, &leave);
    verify(pixelStatus && pixelStatus->text() == QString("坐标：—   RGBA：—"),
        QString("TEST-25"), QString("光标离开图像视图后清除状态栏旧像素值"));
    auto* folderAction = window.findChild<QAction*>(QString("OpenContainingFolder"));
    verify(folderAction && folderAction->isEnabled()
            && view->contextMenuPolicy() == Qt::CustomContextMenu, QString("TEST-26"),
        QString("成功打开本地图片后启用图像右键菜单的所在文件夹操作"));
    window.resize(1100, 750);
    const bool bRounded = !window.mask().contains(QPoint(0, 0))
        && window.mask().contains(window.rect().center());
    window.showMaximized();
    QCoreApplication::processEvents();
    const bool bMaximized = window.mask().isEmpty();
    window.showNormal();
    QCoreApplication::processEvents();
    verify(bRounded && bMaximized && !window.mask().isEmpty(), QString("TEST-27"),
        QString("普通窗口四角裁切，最大化解除裁切，还原后恢复圆角"));
    view->fitToWindow();
    QMouseEvent finalHover(QEvent::MouseMove, view->mapFromScene(QPointF(12, 18)),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &finalHover);
    window.grab().save(QDir(outputRoot).filePath(QString("优化后界面.png")));
    window.close();
    MainWindow restored;
    verify(restored.size() == window.size()
            && QSettings().value(QString("files/lastDirectory")).toString() == directory.path(),
        QString("TEST-21"), QString("重新创建窗口恢复尺寸并记住最近打开目录"));
}

void testHistogramSmoothing()
{
    QVector<quint64> counts(256, 0);
    counts[0] = 1200;
    counts[128] = 1000;
    counts[255] = 700;
    const auto raw = ui::HistogramWidget::smoothCounts(counts, 0.0);
    const auto mild = ui::HistogramWidget::smoothCounts(counts, 1.0);
    const auto strong = ui::HistogramWidget::smoothCounts(counts, 4.0);
    const auto flat = ui::HistogramWidget::smoothCounts(QVector<quint64>(256, 50), 4.0);
    double dSum = 0.0;
    bool bValid = true;
    for (int nBin = 0; nBin < 256; ++nBin) {
        dSum += strong[nBin];
        bValid = bValid && raw[nBin] == static_cast<double>(counts[nBin])
            && strong[nBin] >= 0.0 && std::abs(flat[nBin] - 50.0) < 1e-9;
    }
    verify(bValid && std::abs(dSum - 2900.0) < 1e-8
            && mild[128] < raw[128] && strong[128] < mild[128]
            && ui::HistogramWidget::smoothCounts({}, 1.0).isEmpty(),
        QString("TEST-38"), QString("高斯平滑降低尖峰，关闭还原原始计数，边缘总量守恒且平坦分布保持不变"));
}

void testColorHistograms(const QString& outputRoot)
{
    QImage image(3, 2, QImage::Format_RGB888);
    image.fill(QColor(10, 20, 30));
    image.setPixelColor(1, 0, QColor(200, 20, 50));
    image.setPixelColor(2, 1, QColor(255, 255, 255));
    const auto result = core::analysis::ImageAnalysis::analyze(image, QRect(0, 0, 2, 2));
    bool bCounts = result.ok() && result.bColor && result.nPixelCount == 4;
    for (const auto& histogram : result.rgbHistograms) {
        quint64 nSum = 0;
        for (quint64 nCount : histogram) { nSum += nCount; }
        bCounts = bCounts && histogram.size() == 256 && nSum == 4;
    }
    verify(bCounts && result.rgbHistograms[0][10] == 3 && result.rgbHistograms[0][200] == 1
            && result.rgbHistograms[1][20] == 4 && result.rgbHistograms[2][30] == 3
            && result.rgbHistograms[2][50] == 1 && result.rgbHistograms[0][255] == 0,
        QString("TEST-30"), QString("彩色 ROI 的 RGB 各通道独立计数、总数守恒，排除 ROI 外像素且不混淆 RGB/BGR"));
    QImage gray(2, 2, QImage::Format_Grayscale8);
    gray.fill(42);
    const auto grayResult = core::analysis::ImageAnalysis::analyze(gray, gray.rect());
    verify(grayResult.ok() && !grayResult.bColor && grayResult.grayHistogram[42] == 4,
        QString("TEST-31"), QString("灰度图继续使用单通道直方图"));

    MainWindow window;
    window.resize(1320, 1000);
    auto* dock = window.findChild<QDockWidget*>(QString("AnalysisDock"));
    auto* panel = window.findChild<ui::AnalysisPanel*>();
    auto* group = panel->findChild<QGroupBox*>(QString("HistogramGroup"));
    window.show();
    dock->show();
    panel->setStatistics(result);
    QCoreApplication::processEvents();
    auto* histogram = panel->findChild<ui::HistogramWidget*>(QString("Histogram"));
    const bool bRgbVisible = histogram && histogram->isVisible()
        && panel->findChildren<ui::HistogramWidget*>().size() == 1
        && panel->findChildren<QTabWidget*>().isEmpty()
        && group->title() == QString("RGB 三通道直方图");
    const int nLeft = std::max(48, histogram->fontMetrics().horizontalAdvance(QString("4")) + 8);
    const int nHoverX = nLeft + qRound(20.0 * (histogram->width() - nLeft - 13) / 255.0);
    QMouseEvent hover(QEvent::MouseMove, QPoint(nHoverX, 40), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(histogram, &hover);
    const bool bTooltip = QToolTip::text() == QString("灰度档 20：R 0 / G 4 / B 0 像素");
    auto* smoothing = panel->findChild<QComboBox*>(QString("HistogramSmoothing"));
    bool bSmoothing = smoothing && smoothing->currentData().toDouble() == 1.0;
    for (int nIndex = 0; nIndex < smoothing->count(); ++nIndex) {
        smoothing->setCurrentIndex(nIndex);
        QCoreApplication::sendEvent(histogram, &hover);
        bSmoothing = bSmoothing && QToolTip::text() == QString("灰度档 20：R 0 / G 4 / B 0 像素");
    }
    ui::AnalysisPanel restored;
    bSmoothing = bSmoothing
        && restored.findChild<QComboBox*>(QString("HistogramSmoothing"))->currentData().toDouble() == 4.0;
    smoothing->setCurrentIndex(1);
    verify(bSmoothing, QString("TEST-39"), QString("四档平滑不改变悬停原始计数，默认轻度且保存用户选择"));
    panel->setStatistics(grayResult);
    verify(bRgbVisible && bTooltip && histogram->isVisible() && group->title() == QString("灰度直方图"),
        QString("TEST-32"), QString("单图叠加 RGB、悬停显示同一档三通道计数，切换灰度清除旧通道，无独立页签"));
    panel->setPixel(QPoint(0, 0), QColor(130, 130, 130), true);
    bool bAchromatic = false;
    for (const auto* label : panel->findChildren<QLabel*>()) {
        bAchromatic = bAchromatic || label->text() == QString("无色相, 0, 130");
    }
    verify(bAchromatic, QString("TEST-37"), QString("灰度像素没有定义色相，显示无色相而非负一度"));
    QImage demo(640, 360, QImage::Format_RGB888);
    for (int nY = 0; nY < demo.height(); ++nY) {
        for (int nX = 0; nX < demo.width(); ++nX) {
            demo.setPixelColor(nX, nY, QColor(nX * 255 / 639, nY * 255 / 359, (nX + nY) % 256));
        }
    }
    const QString demoPath = QDir(outputRoot).filePath(QString("RGB示例.png"));
    demo.save(demoPath);
    window.openFile(demoPath);
    auto* view = window.findChild<ui::ImageView*>();
    const bool bLoaded = waitUntil([&]() { return view->image().size() == demo.size(); });
    panel->findChild<QPushButton*>(QString("AnalyzeFullImage"))->click();
    const bool bAnalyzed = waitUntil([&]() { return group->title() == QString("RGB 三通道直方图"); });
    bool bPixelCount = false;
    for (const auto* label : panel->findChildren<QLabel*>()) {
        bPixelCount = bPixelCount || label->text() == QString("230400");
    }
    verify(bLoaded && bAnalyzed && bPixelCount, QString("TEST-33"),
        QString("实际打开彩色 PNG 并点击分析整张图，后台发布 RGB 三通道结果且统计像素数正确"));
    auto* thumbnails = window.findChild<ui::ThumbnailBar*>();
    waitUntil([&]() {
        for (int nIndex = 0; nIndex < thumbnails->count(); ++nIndex) {
            if (thumbnails->item(nIndex)->icon().isNull()) { return false; }
        }
        return true;
    });
    window.grab().save(QDir(outputRoot).filePath(QString("RGB三通道界面.png")));
    window.close();
}

void testOverlayNavigation(const QString& outputRoot)
{
    QTemporaryDir directory(QDir(outputRoot).filePath(QString("arrows-XXXXXX")));
    QImage first(1200, 900, QImage::Format_RGB32);
    first.fill(Qt::red);
    QImage second(first.size(), first.format());
    second.fill(Qt::blue);
    const QString firstPath = directory.filePath(QString("图1.png"));
    const QString secondPath = directory.filePath(QString("图2.png"));
    first.save(firstPath);
    second.save(secondPath);
    MainWindow window;
    window.show();
    auto* view = window.findChild<ui::ImageView*>();
    auto* previous = view->findChild<QToolButton*>(QString("PreviousImageButton"));
    auto* next = view->findChild<QToolButton*>(QString("NextImageButton"));
    const bool bEmptyHidden = previous && next && !previous->isVisible() && !next->isVisible();
    window.openFile(firstPath);
    const bool bFirst = waitUntil([&]() { return view->image() == first; });
    bool bNavigation = bEmptyHidden && bFirst && previous->isVisible() && next->isVisible()
        && !previous->isEnabled() && next->isEnabled()
        && previous->toolButtonStyle() == Qt::ToolButtonIconOnly && !previous->icon().isNull();
    next->click();
    bNavigation = waitUntil([&]() { return view->image() == second; }) && bNavigation
        && previous->isEnabled() && !next->isEnabled();
    previous->click();
    bNavigation = waitUntil([&]() { return view->image() == first; }) && bNavigation;
    for (QToolBar* toolbar : window.findChildren<QToolBar*>()) {
        bNavigation = bNavigation && !toolbar->actions().contains(previous->defaultAction())
            && !toolbar->actions().contains(next->defaultAction());
    }
    verify(bNavigation, QString("TEST-28"),
        QString("两侧图标点击可往返翻图、首尾禁用、空图隐藏，工具栏不再显示翻图文字按钮"));

    const auto anchored = [&]() {
        return previous->parentWidget() == view->viewport()
            && previous->x() == 12
            && next->x() == view->viewport()->width() - next->width() - 12
            && previous->y() == (view->viewport()->height() - previous->height()) / 2
            && next->y() == previous->y();
    };
    window.resize(1200, 800);
    view->actualSize();
    QCoreApplication::processEvents();
    bool bAnchored = anchored();
    view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum());
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    bAnchored = bAnchored && anchored();
    window.resize(1000, 700);
    QCoreApplication::processEvents();
    verify(bAnchored && anchored(), QString("TEST-29"),
        QString("图像原尺寸滚动和窗口缩放后，翻图箭头始终固定在图像视口左右两侧居中"));
    const auto press = [&](int nKey) {
        QKeyEvent key(QEvent::KeyPress, nKey, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &key);
    };
    const auto boundaryKeepsPosition = [&](int nKey) {
        view->actualSize();
        view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum() / 2);
        const int nBefore = view->horizontalScrollBar()->value();
        for (int nRepeat = 0; nRepeat < 5; ++nRepeat) { press(nKey); }
        return view->horizontalScrollBar()->value() == nBefore;
    };
    bool bKeys = boundaryKeepsPosition(Qt::Key_Left) && view->image() == first;
    press(Qt::Key_Right);
    bKeys = waitUntil([&]() { return view->image() == second; }) && bKeys;
    bKeys = boundaryKeepsPosition(Qt::Key_Right) && view->image() == second && bKeys;
    press(Qt::Key_Left);
    bKeys = waitUntil([&]() { return view->image() == first; }) && bKeys;
    verify(bKeys, QString("TEST-34"), QString("首尾连续方向键不平移图像，非边界左右键仍正确往返翻图"));
    auto* parameters = window.findChild<ui::PreprocessPanel*>();
    auto* slider = parameters->findChild<QSlider*>();
    auto* preprocessDock = window.findChild<QDockWidget*>(QString("PreprocessDock"));
    preprocessDock->show();
    core::processing::ProcessingParameters enabled;
    enabled.bEnabled = true;
    parameters->setParameters(enabled);
    slider->setValue(0);
    slider->setFocus();
    QKeyEvent adjust(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QCoreApplication::sendEvent(slider, &adjust);
    QCoreApplication::processEvents();
    verify(slider->value() == 1 && view->image() == first
            && previous->defaultAction()->shortcutContext() == Qt::WidgetWithChildrenShortcut
            && next->defaultAction()->shortcutContext() == Qt::WidgetWithChildrenShortcut,
        QString("TEST-36"), QString("预处理滑块中的左右键调节参数，不抢占为翻图快捷键"));
    bool bAbout = false;
    QTimer::singleShot(0, &window, [&]() {
        auto* about = window.findChild<QDialog*>(QString("AboutDialog"));
        auto* information = about ? about->findChild<QLabel*>(QString("AboutInformation")) : nullptr;
        bAbout = information && information->text().contains(QString("Blazar"))
            && information->text().contains(QString("mailto:blazarlin@gmail.com"));
        if (about) { about->accept(); }
    });
    QMetaObject::invokeMethod(&window, "onAbout");
    verify(bAbout, QString("TEST-35"), QString("关于窗口显示作者 Blazar 与可点击邮箱地址"));
    window.close();
}

void testFeedbackWorkflow(const QString& outputRoot)
{
    QTemporaryDir directory(QDir(outputRoot).filePath(QString("反馈验收-XXXXXX")));
    QImage first(640, 360, QImage::Format_RGB32);
    first.fill(QColor(20, 30, 40));
    QImage second(first.size(), first.format());
    second.fill(QColor(80, 90, 100));
    const QString firstPath = directory.filePath(QString("原图 1.png"));
    const QString secondPath = directory.filePath(QString("原图 2.png"));
    first.save(firstPath); second.save(secondPath);
    MainWindow window;
    window.show();
    auto* view = window.findChild<ui::ImageView*>();
    auto* panel = window.findChild<ui::AnalysisPanel*>();
    auto* analysisDock = window.findChild<QDockWidget*>(QString("AnalysisDock"));
    auto* histogramGroup = panel->findChild<QGroupBox*>(QString("HistogramGroup"));
    auto* preprocess = window.findChild<ui::PreprocessPanel*>();
    const auto bHasStatistics = [&]() { return histogramGroup->title() != QString("直方图"); };
    const auto settle = []() {
        QElapsedTimer timer; timer.start();
        waitUntil([&]() { return timer.elapsed() > 160; });
    };
    window.openFile(firstPath);
    waitUntil([&]() { return view->image() == first; });
    bool bManual = !analysisDock->isVisible() && view->hasFocus();
    QMetaObject::invokeMethod(&window, "startRoiAnalysis", Q_ARG(QRect, first.rect()));
    settle();
    bManual = bManual && !bHasStatistics() && !analysisDock->isVisible();
    analysisDock->show();
    settle();
    bManual = bManual && !bHasStatistics();
    panel->findChild<QPushButton*>(QString("AnalyzeFullImage"))->click();
    bManual = waitUntil(bHasStatistics) && bManual;
    window.openFile(secondPath);
    waitUntil([&]() { return view->image() == second; });
    settle();
    bManual = bManual && !bHasStatistics();
    QMetaObject::invokeMethod(&window, "startRoiAnalysis", Q_ARG(QRect, first.rect()));
    analysisDock->hide();
    settle();
    bManual = bManual && !bHasStatistics();
    verify(bManual, QString("TEST-40"), QString("分析默认隐藏，显示和换图不自动计算，隐藏时拒绝请求并作废在途结果"));

    core::processing::ProcessingParameters parameters;
    parameters.bEnabled = true;
    parameters.nBrightness = 10;
    preprocess->setParameters(parameters);
    waitUntil([&]() { return view->image().pixelColor(0, 0).red() == 90; });
    QAction* compare = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == QKeySequence(QString("Ctrl+D"))) { compare = action; }
    }
    compare->setChecked(true);
    const QPoint from = view->mapFromScene(QPointF(320, 180));
    const QPoint to = view->mapFromScene(QPointF(210, 180));
    QMouseEvent press(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent drag(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &press);
    QCoreApplication::sendEvent(view->viewport(), &drag);
    QCoreApplication::sendEvent(view->viewport(), &release);
    const double dSplit = view->comparisonSplit();
    parameters.nBrightness = 25;
    preprocess->setParameters(parameters);
    bool bCompare = compare->isChecked() && !compare->isEnabled() && dSplit < 0.4;
    bCompare = waitUntil([&]() { return view->image().pixelColor(0, 0).red() == 105; }) && bCompare;
    verify(bCompare && compare->isChecked() && compare->isEnabled() && view->comparisonEnabled()
            && std::abs(view->comparisonSplit() - dSplit) < 0.001,
        QString("TEST-41"), QString("修改参数时保留对比开关，结果完成后保持原分割位置"));

    analysisDock->show();
    auto* source = panel->findChild<QComboBox*>(QString("AnalysisSource"));
    auto* sourceLabel = panel->findChild<QLabel*>(QString("AnalysisDataSource"));
    QMetaObject::invokeMethod(&window, "startRoiAnalysis", Q_ARG(QRect, QRect(0, 0, 10, 10)));
    waitUntil(bHasStatistics);
    const auto containsLabel = [&](const QString& text) {
        for (auto* label : panel->findChildren<QLabel*>()) { if (label->text() == text) { return true; } }
        return false;
    };
    bool bSource = containsLabel(QString("105 / 105 / 105.00 / 0.00"))
        && sourceLabel->text().contains(QString("处理结果"));
    source->setCurrentIndex(1);
    bSource = !bHasStatistics() && bSource;
    QMetaObject::invokeMethod(&window, "startRoiAnalysis", Q_ARG(QRect, QRect(0, 0, 10, 10)));
    waitUntil(bHasStatistics);
    bSource = bSource && containsLabel(QString("80 / 80 / 80.00 / 0.00"));
    for (const QPoint& position : { QPoint(40, 50), QPoint(600, 50) }) {
        QMouseEvent hover(QEvent::MouseMove, view->mapFromScene(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &hover);
        bSource = bSource && panel->findChild<QLabel*>(QString("PixelSource"))->text()
            == (position.x() == 40 ? QString("原图") : QString("处理结果"));
    }
    verify(bSource, QString("TEST-42"), QString("ROI 单独选择原图或处理结果，切换清空旧数据，对比两侧取样标注实际来源"));

    auto* brightness = preprocess->findChild<QSpinBox*>(QString("BrightnessValue"));
    auto* slider = preprocess->findChild<QSlider*>();
    brightness->setValue(-7);
    bool bInputs = slider->value() == -7 && preprocess->parameters().nBrightness == -7;
    slider->setValue(43);
    bInputs = bInputs && brightness->value() == 43;
    brightness->parentWidget()->findChild<QToolButton*>(QString("ResetParameter"))->click();
    bInputs = bInputs && brightness->value() == 0 && slider->value() == 0;
    auto* gamma = preprocess->findChild<QSpinBox*>(QString("GammaValue"));
    gamma->setValue(5000);
    verify(bInputs && gamma->value() == 500 && preprocess->parameters().dGamma == 5.0,
        QString("TEST-43"), QString("数值输入与滑块双向同步，单项复原保持其他参数，越界值限制在有效范围"));
    preprocess->setProcessingEnabled(false);

    panel->findChild<QToolButton*>(QString("analysis/pixelExpanded"))->setChecked(false);
    panel->findChild<QToolButton*>(QString("analysis/statisticsExpanded"))->setChecked(false);
    ui::AnalysisPanel restored;
    verify(!restored.findChild<QToolButton*>(QString("analysis/pixelExpanded"))->isChecked()
            && !restored.findChild<QToolButton*>(QString("analysis/statisticsExpanded"))->isChecked(),
        QString("TEST-44"), QString("像素和统计组可独立折叠，重新创建面板保留展开状态"));

    auto* thumbnails = window.findChild<ui::ThumbnailBar*>();
    auto* search = window.findChild<QLineEdit*>(QString("ThumbnailSearch"));
    search->setText(QString("原图 1"));
    bool bThumbs = thumbnails->visibleFileCount() == 1 && thumbnails->item(1)->isHidden();
    bThumbs = waitUntil([&]() { return !thumbnails->item(0)->icon().isNull(); }) && bThumbs;
    search->setText(QString("不存在"));
    bThumbs = bThumbs && thumbnails->visibleFileCount() == 0;
    window.findChild<QToolButton*>(QString("LocateThumbnail"))->click();
    bThumbs = bThumbs && search->text().isEmpty() && thumbnails->currentRow() == 1;
    auto* sizes = window.findChild<QComboBox*>(QString("ThumbnailSize"));
    sizes->setCurrentIndex(2);
    bThumbs = bThumbs && thumbnails->iconSize() == QSize(160, 110);
    window.findChild<QAction*>(QString("ToggleThumbnails"))->setChecked(false);
    QKeyEvent previous(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &previous);
    bThumbs = waitUntil([&]() { return view->image() == first; }) && bThumbs;
    verify(bThumbs && !thumbnails->isVisible(), QString("TEST-45"),
        QString("缩略图搜索含中文空格、无匹配、定位当前与大小切换正确，隐藏时方向键仍可翻图"));
    // 留下可复用的合成图界面证据，不包含用户图像。
    window.findChild<QAction*>(QString("ToggleThumbnails"))->setChecked(true);
    sizes->setCurrentIndex(0);
    analysisDock->show();
    panel->findChild<QPushButton*>(QString("AnalyzeFullImage"))->click();
    waitUntil(bHasStatistics);
    window.resize(1380, 950);
    QCoreApplication::processEvents();
    window.grab().save(QDir(outputRoot).filePath(QString("反馈优化界面.png")));
    window.close();
    MainWindow reopened;
    reopened.show();
    verify(!reopened.findChild<QDockWidget*>(QString("AnalysisDock"))->isVisible(),
        QString("TEST-46"), QString("即使上次退出时分析可见，重新启动仍默认隐藏"));
    reopened.close();
    QSettings().setValue(QString("ui/thumbnailSize"), 1);
    QSettings().setValue(QString("analysis/pixelExpanded"), true);
    QSettings().setValue(QString("analysis/statisticsExpanded"), true);
}

void testForwardedFiles(const QString& outputRoot)
{
    QTemporaryDir directory(QDir(outputRoot).filePath(QString("多文件-XXXXXX")));
    QTemporaryDir other(QDir(outputRoot).filePath(QString("其他目录-XXXXXX")));
    QImage image(16, 16, QImage::Format_RGB32); image.fill(Qt::red);
    const QString first = directory.filePath(QString("中文 空格 1.png"));
    const QString second = other.filePath(QString("中文 空格 2.png"));
    image.save(first); image.save(second);
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    const QString oldName = QCoreApplication::applicationName();
    const QString name = oldName + QString::number(QCoreApplication::applicationPid());
    QCoreApplication::setApplicationName(name);
    qputenv("IMAGEVIEWER_TEST_INSTANCE", name.toUtf8());
    Application instance(app);
    const bool bPrimary = instance.startup();
    QStringList received;
    int nRequests = 0;
    QObject::connect(&instance, &Application::filesRequested, &instance, [&](const QStringList& paths) {
        received = paths; ++nRequests;
    });
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), { QString("--ipc-client"), first, second, first });
    const bool bFinished = waitUntil([&]() { return child.state() == QProcess::NotRunning; });
    const bool bFiles = bPrimary && bFinished && child.exitCode() == 0 && received == QStringList({ first, second });
    child.start(QCoreApplication::applicationFilePath(), { QString("--ipc-client") });
    const bool bActivated = waitUntil([&]() { return child.state() == QProcess::NotRunning; });
    verify(bFiles && bActivated && child.exitCode() == 0 && nRequests == 2 && received.isEmpty(),
        QString("TEST-47"), QString("真实子进程转发中文空格及多文件参数并确认接收，重复路径去重，无参数请求也通知激活"));
    QCoreApplication::setApplicationName(oldName);
    qunsetenv("IMAGEVIEWER_TEST_INSTANCE");
    core::navigation::DirectoryModel model;
    model.setFileList({ first, second }, first);
    bool bBatch = model.count() == 2 && model.moveNext() && model.currentPath() == second;
    model.loadForFile(second);
    bBatch = bBatch && model.count() == 2 && model.currentIndex() == 1;
    QFile::remove(first);
    model.loadForFile(second, true);
    bBatch = bBatch && model.count() == 1 && model.currentPath() == second;
    model.setFileList({ second }, second);
    verify(bBatch && model.count() == 1, QString("TEST-48"),
        QString("跨目录多文件按传入顺序浏览，刷新剔除删除项，单文件打开恢复目录模式"));
    const auto canceled = std::make_shared<std::atomic_bool>(true);
    const auto result = core::analysis::ImageAnalysis::analyze(image, image.rect(), canceled);
    verify(!result.ok() && result.nPixelCount == 0 && result.grayHistogram.isEmpty(),
        QString("TEST-49"), QString("已取消的分析不分配直方图、不发布部分统计"));
}

} // namespace

int main(int argc, char* argv[])
{
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
    });
    SetConsoleOutputCP(CP_UTF8);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("WINDIR")).filePath(QString("Fonts")).toUtf8());
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    if (app.arguments().contains(QString("--ipc-client"))) {
        QCoreApplication::setApplicationName(qEnvironmentVariable("IMAGEVIEWER_TEST_INSTANCE"));
        Application client(&app);
        return client.startup() ? 23 : (client.startupError().isEmpty() ? 0 : 24);
    }
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QCoreApplication::setOrganizationName(QString("ImageViewerTests"));
    QCoreApplication::setApplicationName(QString("ImageViewerTests"));
    QStandardPaths::setTestModeEnabled(true);
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    const QString outputRoot = QDir(QCoreApplication::applicationDirPath())
        .filePath(QString("ImageViewerCoreTestsData"));
    QDir().mkpath(outputRoot);
    qInfo().noquote() << QString("核心测试输出目录：%1").arg(outputRoot);

    testDirectoryNavigation(outputRoot);
    testProcessorPassThrough();
    testBrightnessSaturation();
    testThresholdAndDimensions();
    testChannelSelection();
    testRoiStatistics();
    testProcessingPreset();
    testCacheInvalidation(outputRoot);
    testImagePyramid();
    testRefreshAndAtomicSave(outputRoot);
    testViewInteractions();
    testWindowWorkflow(outputRoot);
    testExifThumbnail(outputRoot);
    testOverlayNavigation(outputRoot);
    testHistogramSmoothing();
    testColorHistograms(outputRoot);
    testFeedbackWorkflow(outputRoot);
    testForwardedFiles(outputRoot);

    qInfo().noquote() << QString("测试完成：失败 %1 项").arg(nFailedTests);
    return nFailedTests == 0 ? 0 : 1;
}
