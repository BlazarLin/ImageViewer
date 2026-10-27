// 2026-09-06
// 功能：实现亮度、对比度、Gamma、通道、平滑、锐化、阈值、边缘和形态学管线。
// 目的：提供稳定、有界、可异步调用的 OpenCV 实时预处理。
#include "ImageProcessor.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTransform>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <vector>

namespace core::processing {
namespace {

int oddKernel(int nValue, int nMaximum = 31)
{
    const int nClamped = std::clamp(nValue, 1, nMaximum);
    return (nClamped % 2 == 0) ? std::min(nClamped + 1, nMaximum) : nClamped;
}

cv::Mat qImageToWorkingMat(const QImage& image)
{
    if (image.isNull()) {
        return {};
    }
    if (util::grayFormat(image)) {
        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        return cv::Mat(gray.height(), gray.width(), CV_8UC1,
            const_cast<uchar*>(gray.constBits()), gray.bytesPerLine()).clone();
    }

    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
        const_cast<uchar*>(rgb.constBits()), rgb.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

QImage workingMatToQImage(const cv::Mat& mat)
{
    if (mat.empty() || mat.depth() != CV_8U) {
        return {};
    }
    if (mat.channels() == 1) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
            QImage::Format_Grayscale8).copy();
    }
    if (mat.channels() == 3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
            QImage::Format_RGB888).copy();
    }
    return {};
}

cv::Mat toGray(const cv::Mat& source)
{
    if (source.channels() == 1) {
        return source;
    }
    cv::Mat gray;
    cv::cvtColor(source, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

void applyChannel(cv::Mat& image, ChannelView channel)
{
    if (channel == ChannelView::Original || image.channels() == 1) {
        return;
    }
    if (channel == ChannelView::Gray) {
        image = toGray(image);
        return;
    }

    int nChannel = 0;
    if (channel == ChannelView::Green) {
        nChannel = 1;
    } else if (channel == ChannelView::Red) {
        nChannel = 2;
    }
    cv::extractChannel(image, image, nChannel);
}

void applyTone(cv::Mat& image, const ProcessingParameters& parameters)
{
    if (std::abs(parameters.dContrast - 1.0) > 0.0001 || parameters.nBrightness != 0) {
        image.convertTo(image, -1, parameters.dContrast, parameters.nBrightness);
    }
    if (std::abs(parameters.dGamma - 1.0) > 0.0001) {
        std::array<uchar, 256> values{};
        const double dInverseGamma = 1.0 / std::clamp(parameters.dGamma, 0.1, 5.0);
        for (int nValue = 0; nValue < 256; ++nValue) {
            values[static_cast<size_t>(nValue)] = cv::saturate_cast<uchar>(
                std::pow(nValue / 255.0, dInverseGamma) * 255.0);
        }
        const cv::Mat table(1, 256, CV_8UC1, values.data());
        cv::LUT(image, table, image);
    }
}

void applySmoothAndSharpen(cv::Mat& image, const ProcessingParameters& parameters)
{
    const int nKernel = oddKernel(parameters.nSmoothKernel);
    switch (parameters.smooth) {
    case SmoothMode::Box:
        cv::blur(image, image, cv::Size(nKernel, nKernel));
        break;
    case SmoothMode::Gaussian:
        cv::GaussianBlur(image, image, cv::Size(nKernel, nKernel), 0.0);
        break;
    case SmoothMode::Median:
        if (nKernel > 1) {
            cv::medianBlur(image, image, nKernel);
        }
        break;
    case SmoothMode::None:
        break;
    }

    const double dAmount = std::clamp(parameters.dSharpenAmount, 0.0, 3.0);
    if (dAmount > 0.0001) {
        cv::Mat blurred;
        cv::GaussianBlur(image, blurred, cv::Size(0, 0), 1.2);
        cv::addWeighted(image, 1.0 + dAmount, blurred, -dAmount, 0.0, image);
    }
}

void applyThresholdOrEdge(cv::Mat& image, const ProcessingParameters& parameters)
{
    if (parameters.edge != EdgeMode::None) {
        const cv::Mat gray = toGray(image);
        switch (parameters.edge) {
        case EdgeMode::Sobel: {
            cv::Mat gradX;
            cv::Mat gradY;
            cv::Sobel(gray, gradX, CV_16S, 1, 0, 3);
            cv::Sobel(gray, gradY, CV_16S, 0, 1, 3);
            cv::Mat absX;
            cv::Mat absY;
            cv::convertScaleAbs(gradX, absX);
            cv::convertScaleAbs(gradY, absY);
            cv::addWeighted(absX, 0.5, absY, 0.5, 0.0, image);
            break;
        }
        case EdgeMode::Laplacian: {
            cv::Mat laplacian;
            cv::Laplacian(gray, laplacian, CV_16S, 3);
            cv::convertScaleAbs(laplacian, image);
            break;
        }
        case EdgeMode::Canny:
            cv::Canny(gray, image,
                std::clamp(parameters.nCannyLow, 0, 255),
                std::clamp(parameters.nCannyHigh, 0, 255));
            break;
        case EdgeMode::None:
            break;
        }
        return;
    }

    if (parameters.threshold != ThresholdMode::None) {
        image = toGray(image);
        const int nType = parameters.threshold == ThresholdMode::Otsu
            ? cv::THRESH_BINARY | cv::THRESH_OTSU
            : cv::THRESH_BINARY;
        cv::threshold(image, image, std::clamp(parameters.nThreshold, 0, 255), 255, nType);
    }
}

void applyMorphology(cv::Mat& image, const ProcessingParameters& parameters)
{
    if (parameters.morphology == MorphologyMode::None) {
        return;
    }
    const int nKernel = oddKernel(parameters.nMorphKernel, 21);
    const int nIterations = std::clamp(parameters.nMorphIterations, 1, 10);
    const cv::Mat element = cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(nKernel, nKernel));
    switch (parameters.morphology) {
    case MorphologyMode::Erode:
        cv::erode(image, image, element, cv::Point(-1, -1), nIterations);
        break;
    case MorphologyMode::Dilate:
        cv::dilate(image, image, element, cv::Point(-1, -1), nIterations);
        break;
    case MorphologyMode::Open:
        cv::morphologyEx(image, image, cv::MORPH_OPEN, element,
            cv::Point(-1, -1), nIterations);
        break;
    case MorphologyMode::Close:
        cv::morphologyEx(image, image, cv::MORPH_CLOSE, element,
            cv::Point(-1, -1), nIterations);
        break;
    case MorphologyMode::None:
        break;
    }
}

} // namespace

ProcessingResult ImageProcessor::process(const QImage& source,
    const ProcessingParameters& parameters, const util::Cancellation& canceled)
{
    ProcessingResult result;
    QElapsedTimer timer;
    timer.start();

    if (source.isNull()) {
        result.error = QCoreApplication::translate("ImageProcessor", "没有可处理的图像");
        return result;
    }
    if (util::canceled(canceled)) { return {}; }
    const bool bIdentity = parameters.channel == ChannelView::Original
        && parameters.nBrightness == 0 && parameters.dContrast == 1.0 && parameters.dGamma == 1.0
        && parameters.smooth == SmoothMode::None && parameters.dSharpenAmount == 0.0
        && parameters.threshold == ThresholdMode::None && parameters.edge == EdgeMode::None
        && parameters.morphology == MorphologyMode::None;
    if (!parameters.bEnabled || bIdentity) {
        result.image = source;
        result.nElapsedMs = timer.elapsed();
        return result;
    }

    try {
        cv::Mat working = qImageToWorkingMat(source);
        if (working.empty()) {
            result.error = QCoreApplication::translate("ImageProcessor", "不支持当前图像格式");
            return result;
        }
        applyChannel(working, parameters.channel);
        if (util::canceled(canceled)) { return {}; }
        applyTone(working, parameters);
        if (util::canceled(canceled)) { return {}; }
        applySmoothAndSharpen(working, parameters);
        if (util::canceled(canceled)) { return {}; }
        applyThresholdOrEdge(working, parameters);
        if (util::canceled(canceled)) { return {}; }
        applyMorphology(working, parameters);
        if (util::canceled(canceled)) { return {}; }
        result.image = workingMatToQImage(working);
        // 颜色处理不改变覆盖率；直通路径保留原始精度，处理路径明确为 8 位。
        if (source.hasAlphaChannel() && !result.image.isNull()) {
            QImage output = result.image.convertToFormat(QImage::Format_ARGB32);
            for (int nY = 0; nY < output.height(); ++nY) {
                if (util::canceled(canceled)) { return {}; }
                QRgb* row = reinterpret_cast<QRgb*>(output.scanLine(nY));
                for (int nX = 0; nX < output.width(); ++nX) {
                    row[nX] = (row[nX] & 0x00ffffff) | (static_cast<quint32>(source.pixelColor(nX, nY).alpha()) << 24);
                }
            }
            result.image = output;
        }
        if (result.image.isNull()) {
            result.error = QCoreApplication::translate("ImageProcessor", "处理结果无法转换为显示图像");
        }
    } catch (const cv::Exception& error) {
        result.error = QCoreApplication::translate("ImageProcessor", "OpenCV 处理失败：%1")
            .arg(QString::fromLocal8Bit(error.what()));
    } catch (const std::exception& error) {
        result.error = QCoreApplication::translate("ImageProcessor", "图像处理失败：%1")
            .arg(QString::fromLocal8Bit(error.what()));
    }
    result.nElapsedMs = timer.elapsed();
    return result;
}

ProcessingResult ImageProcessor::orient(const QImage& source, int nQuarterTurns,
    bool bMirrorHorizontal, bool bMirrorVertical, const util::Cancellation& canceled)
{
    ProcessingResult result;
    QElapsedTimer timer;
    timer.start();
    if (source.isNull()) {
        result.error = QCoreApplication::translate("ImageProcessor", "没有可处理的图像");
        return result;
    }
    if (util::canceled(canceled)) { return {}; }
    int nTurns = nQuarterTurns % 4;
    if (nTurns < 0) { nTurns += 4; }
    if (nTurns == 0 && !bMirrorHorizontal && !bMirrorVertical) {
        result.image = source;
        result.nElapsedMs = timer.elapsed();
        return result;
    }
    result.image = source;
    if (nTurns != 0) {
        result.image = result.image.transformed(QTransform().rotate(nTurns * 90.0),
            Qt::FastTransformation);
    }
    if (util::canceled(canceled)) { return {}; }
    if (bMirrorHorizontal || bMirrorVertical) {
        result.image = result.image.mirrored(bMirrorHorizontal, bMirrorVertical);
    }
    result.nElapsedMs = timer.elapsed();
    return result;
}

ChannelSplitResult ImageProcessor::splitChannels(const QImage& source,
    const QSize& maximumSize, const util::Cancellation& canceled)
{
    ChannelSplitResult result;
    if (source.isNull()) {
        result.error = QCoreApplication::translate("ImageProcessor", "没有可处理的图像");
        return result;
    }
    if (util::canceled(canceled)) { return {}; }
    try {
        const cv::Mat bgr = qImageToWorkingMat(source);
        if (bgr.empty()) {
            result.error = QCoreApplication::translate("ImageProcessor", "不支持当前图像格式");
            return result;
        }
        std::vector<cv::Mat> bgrChannels;
        std::vector<cv::Mat> hsvChannels;
        cv::split(bgr, bgrChannels);
        if (util::canceled(canceled)) { return {}; }
        cv::Mat hsv;
        if (bgr.channels() == 3) {
            cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
            cv::split(hsv, hsvChannels);
        } else {
            // 灰度图没有色相和饱和度；V 使用原灰度，H/S 明确显示为零。
            hsvChannels = {
                cv::Mat::zeros(bgr.rows, bgr.cols, CV_8UC1),
                cv::Mat::zeros(bgr.rows, bgr.cols, CV_8UC1),
                bgrChannels.front()
            };
        }
        if (util::canceled(canceled)) { return {}; }
        const cv::Mat ordered[6] = {
            bgrChannels[std::min<size_t>(2, bgrChannels.size() - 1)],
            bgrChannels[std::min<size_t>(1, bgrChannels.size() - 1)],
            bgrChannels.front(), hsvChannels[0], hsvChannels[1], hsvChannels[2]
        };
        const QSize bounded = maximumSize.isValid() && !maximumSize.isEmpty()
            ? maximumSize : QSize(640, 480);
        const QSize fitted = source.width() > bounded.width() || source.height() > bounded.height()
            ? source.size().scaled(bounded, Qt::KeepAspectRatio) : source.size();
        for (int nIndex = 0; nIndex < 6; ++nIndex) {
            if (util::canceled(canceled)) { return {}; }
            cv::Mat thumbnail;
            if (fitted != source.size()) {
                cv::resize(ordered[nIndex], thumbnail,
                    cv::Size(fitted.width(), fitted.height()), 0.0, 0.0, cv::INTER_AREA);
            } else {
                thumbnail = ordered[nIndex];
            }
            result.images[nIndex] = workingMatToQImage(thumbnail);
        }
    } catch (const cv::Exception& error) {
        result.error = QCoreApplication::translate("ImageProcessor", "OpenCV 处理失败：%1")
            .arg(QString::fromLocal8Bit(error.what()));
    } catch (const std::exception& error) {
        result.error = QCoreApplication::translate("ImageProcessor", "图像处理失败：%1")
            .arg(QString::fromLocal8Bit(error.what()));
    }
    return result;
}

} // namespace core::processing
