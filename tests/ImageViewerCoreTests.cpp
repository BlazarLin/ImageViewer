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
#include "core/processing/ImageProcessor.h"

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

} // namespace

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QCoreApplication app(argc, argv);

    const QString outputRoot = QDir(QCoreApplication::applicationDirPath())
        .filePath(QString("ImageViewerCoreTestsData"));
    QDir().mkpath(outputRoot);
    qInfo().noquote() << QString("核心测试输出目录：%1").arg(outputRoot);

    testDirectoryNavigation(outputRoot);
    testProcessorPassThrough();
    testBrightnessSaturation();
    testThresholdAndDimensions();
    testChannelSelection();

    qInfo().noquote() << QString("测试完成：失败 %1 项").arg(nFailedTests);
    return nFailedTests == 0 ? 0 : 1;
}
