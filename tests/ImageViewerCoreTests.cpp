// 2026-09-06
// 功能：独立验证目录导航与 OpenCV 预处理核心行为。
// 目的：在不启动主窗口的情况下提供可重复的中文业务验收输出。
#include <Windows.h>
#include <QApplication>
#include <QAction>
#include <QBuffer>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QLabel>
#include <functional>
#include "app/MainWindow.h"
#include "ui/ImageView.h"
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
    window.resize(1100, 750);
    window.grab().save(QDir(outputRoot).filePath(QString("优化后界面.png")));
    window.close();
    MainWindow restored;
    verify(restored.size() == window.size()
            && QSettings().value(QString("files/lastDirectory")).toString() == directory.path(),
        QString("TEST-21"), QString("重新创建窗口恢复尺寸并记住最近打开目录"));
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
    QApplication app(argc, argv);
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

    qInfo().noquote() << QString("测试完成：失败 %1 项").arg(nFailedTests);
    return nFailedTests == 0 ? 0 : 1;
}
