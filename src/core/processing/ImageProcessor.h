// 2026-09-06
// 功能：定义实时图像预处理管线的参数与结果。
// 目的：以无 UI 依赖的纯计算接口支撑异步预览和单元测试。
#pragma once

#include "util/BackgroundWork.h"
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

// 2026-10-27：六个通道的缩略图按 R/G/B/H/S/V 顺序存放。
struct ChannelSplitResult {
    QImage images[6];
    QString error;

    bool ok() const
    {
        if (!error.isEmpty()) { return false; }
        for (const QImage& image : images) {
            if (image.isNull()) { return false; }
        }
        return true;
    }
};

class ImageProcessor {
public:
    static ProcessingResult process(const QImage& source,
        const ProcessingParameters& parameters, const util::Cancellation& canceled = {});

    // 旋转后执行镜像；角度为顺时针 90 度的倍数。
    static ProcessingResult orient(const QImage& source, int nQuarterTurns,
        bool bMirrorHorizontal, bool bMirrorVertical,
        const util::Cancellation& canceled = {});

    // 后台生成 RGB/HSV 六通道小图，限制返回结果的内存规模。
    static ChannelSplitResult splitChannels(const QImage& source,
        const QSize& maximumSize = QSize(640, 480),
        const util::Cancellation& canceled = {});
};

} // namespace core::processing

Q_DECLARE_METATYPE(core::processing::ProcessingParameters)
