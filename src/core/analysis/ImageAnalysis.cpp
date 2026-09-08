// 2026-09-06
// 功能：实现 ROI 通道统计与灰度直方图计算。
// 目的：使用一次连续像素遍历提供确定、可测试的分析结果。
#include "ImageAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace core::analysis {

AnalysisResult ImageAnalysis::analyze(const QImage& image, const QRect& requestedRegion,
    const std::shared_ptr<std::atomic_bool>& canceled)
{
    AnalysisResult result;
    if (canceled && canceled->load(std::memory_order_relaxed)) { return result; }
    if (image.isNull()) {
        result.error = QString("图像为空");
        return result;
    }

    const QRect imageBounds(0, 0, image.width(), image.height());
    result.region = requestedRegion.normalized().intersected(imageBounds);
    if (result.region.isEmpty()) {
        result.error = QString("ROI 不在图像范围内");
        return result;
    }

    result.nPixelCount = static_cast<qint64>(result.region.width()) * result.region.height();
    result.grayHistogram.fill(0, 256);
    for (auto& histogram : result.rgbHistograms) {
        histogram.fill(0, 256);
    }
    // 按图像类型选择展示方式，RGB 编码的灰度图允许三条相同通道。
    result.bColor = !image.isGrayscale();

    std::array<double, 3> sums = { 0.0, 0.0, 0.0 };
    std::array<double, 3> squareSums = { 0.0, 0.0, 0.0 };
    for (ChannelStatistics& statistics : result.rgb) {
        statistics.dMinimum = 255.0;
        statistics.dMaximum = 0.0;
    }

    const bool bRgb888 = image.format() == QImage::Format_RGB888;
    const bool bRgb32 = image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32;
    for (int nY = result.region.top(); nY <= result.region.bottom(); ++nY) {
        if (canceled && canceled->load(std::memory_order_relaxed)) { return {}; }
        // 常用格式直接读取行数据；其他格式仅转换 ROI 的一行，避免整图副本。
        const QImage converted = bRgb888 || bRgb32 ? QImage()
            : image.copy(result.region.left(), nY, result.region.width(), 1).convertToFormat(QImage::Format_RGB888);
        const uchar* line = bRgb888 ? image.constScanLine(nY) + result.region.left() * 3
            : (bRgb32 ? nullptr : converted.constScanLine(0));
        const QRgb* pixels = bRgb32 ? reinterpret_cast<const QRgb*>(image.constScanLine(nY)) + result.region.left() : nullptr;
        for (int nX = 0; nX < result.region.width(); ++nX) {
            const int nRed = bRgb32 ? qRed(pixels[nX]) : line[nX * 3 + 0];
            const int nGreen = bRgb32 ? qGreen(pixels[nX]) : line[nX * 3 + 1];
            const int nBlue = bRgb32 ? qBlue(pixels[nX]) : line[nX * 3 + 2];
            const std::array<int, 3> values = { nRed, nGreen, nBlue };
            for (int nChannel = 0; nChannel < 3; ++nChannel) {
                const double dValue = values[nChannel];
                ChannelStatistics& statistics = result.rgb[nChannel];
                statistics.dMinimum = std::min(statistics.dMinimum, dValue);
                statistics.dMaximum = std::max(statistics.dMaximum, dValue);
                ++result.rgbHistograms[nChannel][values[nChannel]];
                sums[nChannel] += dValue;
                squareSums[nChannel] += dValue * dValue;
            }
            const int nGray = std::clamp((77 * nRed + 150 * nGreen + 29 * nBlue + 128) >> 8, 0, 255);
            ++result.grayHistogram[nGray];
        }
    }

    const double dCount = static_cast<double>(result.nPixelCount);
    for (int nChannel = 0; nChannel < 3; ++nChannel) {
        ChannelStatistics& statistics = result.rgb[nChannel];
        statistics.dMean = sums[nChannel] / dCount;
        const double dVariance = std::max(0.0,
            squareSums[nChannel] / dCount - statistics.dMean * statistics.dMean);
        statistics.dStandardDeviation = std::sqrt(dVariance);
    }
    return result;
}

} // namespace core::analysis

