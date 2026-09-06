// 2026-09-06
// 功能：独立验证目录导航与 OpenCV 预处理核心行为。
// 目的：在不启动主窗口的情况下提供可重复的中文业务验收输出。
#include <Windows.h>
#include <QCoreApplication>
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

} // namespace

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QCoreApplication app(argc, argv);
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

    qInfo().noquote() << QString("测试完成：失败 %1 项").arg(nFailedTests);
    return nFailedTests == 0 ? 0 : 1;
}
