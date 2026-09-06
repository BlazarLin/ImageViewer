// 2026-09-06
// 功能：实现以 KiB 为成本单位的解码图像 LRU 缓存。
// 目的：用确定上限换取目录前后翻图的低延迟。
#include "ImageCache.h"

#include <QFileInfo>
#include <QDateTime>
#include <QMutexLocker>

#include <algorithm>
#include <climits>

namespace core::cache {

ImageCache::ImageCache(int nMaximumMebibytes)
{
    cache_.setMaxCost(std::max(1, nMaximumMebibytes) * 1024);
}

bool ImageCache::find(const QString& path, QImage* image)
{
    if (!image) {
        return false;
    }
    QMutexLocker locker(&mutex_);
    QImage* cached = cache_.object(keyForFile(path));
    if (!cached) {
        return false;
    }
    *image = *cached;
    return true;
}

void ImageCache::insert(const QString& path, const QImage& image)
{
    if (image.isNull()) {
        return;
    }
    const qint64 nBytes = static_cast<qint64>(image.bytesPerLine()) * image.height();
    const int nCostKiB = static_cast<int>(std::clamp<qint64>((nBytes + 1023) / 1024, 1, INT_MAX));
    QMutexLocker locker(&mutex_);
    cache_.insert(keyForFile(path), new QImage(image), nCostKiB);
}

void ImageCache::clear()
{
    QMutexLocker locker(&mutex_);
    cache_.clear();
}

QString ImageCache::keyForFile(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    return QString("%1|%2|%3")
        .arg(canonical)
        .arg(info.size())
        .arg(info.lastModified().toMSecsSinceEpoch());
}

} // namespace core::cache
