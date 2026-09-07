#include "ImageLoader.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <exception>
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

static LoadResult decodeImage(const QString& path)
{
    util::ElapsedLog _t(QString("loadImage(%1)").arg(QFileInfo(path).fileName()));

    LoadResult result;
    if (path.isEmpty()) {
        result.error = QCoreApplication::translate("ImageLoader", "图像路径为空");
        return result;
    }

    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        result.error = QCoreApplication::translate("ImageLoader", "文件不存在：%1").arg(path);
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
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QCoreApplication::translate("ImageLoader", "无法读取文件：%1").arg(path);
            return result;
        }
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) {
            result.error = QCoreApplication::translate("ImageLoader", "文件内容为空：%1").arg(path);
            return result;
        }
        const cv::Mat encoded(1, bytes.size(), CV_8UC1,
            const_cast<char*>(bytes.constData()));
        const cv::Mat mat = cv::imdecode(encoded, cv::IMREAD_UNCHANGED);
        if (mat.empty()) {
            result.error = QCoreApplication::translate(
                "ImageLoader", "Qt 和 OpenCV 均无法解码图像：%1").arg(path);
            return result;
        }
        result.image = matToQImage(mat);
        if (result.image.isNull()) {
            result.error = QCoreApplication::translate(
                "ImageLoader", "不支持的图像位深或通道数：%1").arg(path);
            return result;
        }
        return result;
    }
}

LoadResult loadImage(const QString& path)
{
    try {
        return decodeImage(path);
    } catch (const std::exception& exception) {
        return { QImage(), QCoreApplication::translate("ImageLoader", "图像解码失败：%1")
            .arg(QString::fromLocal8Bit(exception.what())) };
    }
}

bool saveImage(const QString& path, const QImage& image, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) { *error = file.errorString(); }
        return false;
    }
    QImageWriter writer(&file, QFileInfo(path).suffix().toLatin1().toLower());
    writer.setQuality(95);
    if (!writer.write(image)) {
        if (error) { *error = writer.errorString(); }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) { *error = file.errorString(); }
        return false;
    }
    if (error) { error->clear(); }
    return true;
}

} // namespace core::loader
