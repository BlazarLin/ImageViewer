#pragma once

#include <QImage>
#include <QString>

namespace core::loader {

// 图像加载结果。error 为空字符串表示成功。
struct LoadResult {
    QImage image;
    QString error;
    bool ok() const { return error.isEmpty() && !image.isNull(); }
};

// 同步加载。优先 QImageReader,失败/特殊格式走 OpenCV。
// 不抛异常,失败时 result.error 描述原因。
LoadResult loadImage(const QString& path);

// 根据扩展名编码并原子替换；失败保留原目标文件。
bool saveImage(const QString& path, const QImage& image, QString* error);

} // namespace core::loader
