#include "ImageLoader.h"

#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "util/ElapsedLog.h"

namespace core::loader {

// OpenCV -> QImage 转换。
// 注意:cv::Mat 的 4 通道为 BGRA 字节序,QImage::Format_ARGB32 在小端机器上内存布局同样是 BGRA 字节,
// 因此 4 通道可以直接使用 Format_ARGB32,无需字节交换(经实测 Qt 5.14 在小端 x86/x64 上成立)。
// Qt 5.14 没有 Format_BGRA8888(QImage::Format_BGRA8888 自 5.15 起引入)。
static QImage matToQImage(const cv::Mat& mat)
{
    if (mat.empty()) {
        return {};
    }

    const int channels = mat.channels();
    const int depth = mat.depth();

    // 仅支持 8U
    if (depth != CV_8U) {
        return {};
    }

    QImage::Format format = QImage::Format_Invalid;
    switch (channels) {
    case 1:
        format = QImage::Format_Grayscale8;
        break;
    case 3:
        format = QImage::Format_BGR888; // cv::Mat 3 通道是 BGR
        break;
    case 4:
        format = QImage::Format_ARGB32; // 内存字节序 = BGRA
        break;
    default:
        return {};
    }

    // .copy() 复制 data,使 QImage 独立于 cv::Mat 生命周期。
    return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), format).copy();
}

LoadResult loadImage(const QString& path)
{
    util::ElapsedLog _t(QStringLiteral("loadImage(%1)").arg(QFileInfo(path).fileName()));

    LoadResult result;
    if (path.isEmpty()) {
        result.error = QStringLiteral("path is empty");
        return result;
    }

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        result.error = QStringLiteral("file not found: %1").arg(path);
        return result;
    }

    // 1. 优先 QImageReader
    {
        QImageReader reader(path);
        reader.setAutoTransform(true); // 自动旋转 EXIF
        QImage img = reader.read();
        if (!img.isNull()) {
            result.image = std::move(img);
            return result;
        }
        // 失败但不致命,继续尝试 OpenCV
    }

    // 2. OpenCV fallback(处理多通道 RAW / TIFF 等)
    {
        const cv::Mat mat = cv::imread(path.toLocal8Bit().constData(), cv::IMREAD_UNCHANGED);
        if (mat.empty()) {
            result.error = QStringLiteral("QImageReader and cv::imread both failed: %1")
                               .arg(path);
            return result;
        }
        result.image = matToQImage(mat);
        if (result.image.isNull()) {
            result.error = QStringLiteral("unsupported cv::Mat layout for %1").arg(path);
            return result;
        }
        return result;
    }
}

} // namespace core::loader