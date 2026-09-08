// 2026-09-06
// 功能：计算图像 ROI 的灰度直方图和 RGB 通道统计量。
// 目的：为可显隐的工业图像分析面板提供无 UI 依赖的计算接口。
#pragma once

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

#include <array>

namespace core::analysis {

struct ChannelStatistics {
    double dMinimum = 0.0;
    double dMaximum = 0.0;
    double dMean = 0.0;
    double dStandardDeviation = 0.0;
};

struct AnalysisResult {
    QRect region;
    qint64 nPixelCount = 0;
    std::array<ChannelStatistics, 3> rgb;
    QVector<quint64> grayHistogram;
    std::array<QVector<quint64>, 3> rgbHistograms;
    bool bColor = false;
    QString error;

    bool ok() const { return error.isEmpty() && nPixelCount > 0 && grayHistogram.size() == 256; }
};

class ImageAnalysis {
public:
    static AnalysisResult analyze(const QImage& image, const QRect& requestedRegion);
};

} // namespace core::analysis

