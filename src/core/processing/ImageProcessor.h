// 2026-09-06
// 功能：定义实时图像预处理管线的参数与结果。
// 目的：以无 UI 依赖的纯计算接口支撑异步预览和单元测试。
#pragma once

#include <QImage>
#include <QMetaType>
#include <QString>

namespace core::processing {

enum class ChannelView { Original, Gray, Blue, Green, Red };
enum class SmoothMode { None, Box, Gaussian, Median };
enum class ThresholdMode { None, Binary, Otsu };
enum class EdgeMode { None, Sobel, Laplacian, Canny };
enum class MorphologyMode { None, Erode, Dilate, Open, Close };

struct ProcessingParameters {
    bool bEnabled = false;
    int nBrightness = 0;
    double dContrast = 1.0;
    double dGamma = 1.0;
    ChannelView channel = ChannelView::Original;
    SmoothMode smooth = SmoothMode::None;
    int nSmoothKernel = 3;
    double dSharpenAmount = 0.0;
    ThresholdMode threshold = ThresholdMode::None;
    int nThreshold = 128;
    EdgeMode edge = EdgeMode::None;
    int nCannyLow = 80;
    int nCannyHigh = 160;
    MorphologyMode morphology = MorphologyMode::None;
    int nMorphKernel = 3;
    int nMorphIterations = 1;
};

struct ProcessingResult {
    QImage image;
    QString error;
    qint64 nElapsedMs = 0;

    bool ok() const { return error.isEmpty() && !image.isNull(); }
};

class ImageProcessor {
public:
    static ProcessingResult process(const QImage& source,
        const ProcessingParameters& parameters);
};

} // namespace core::processing

Q_DECLARE_METATYPE(core::processing::ProcessingParameters)
