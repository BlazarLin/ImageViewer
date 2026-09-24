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
    const QString key = keyForFile(path);
    QMutexLocker locker(&mutex_);
    QImage* cached = cache_.object(key);
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
    const QString key = keyForFile(path);
    QMutexLocker locker(&mutex_);
    if (nCostKiB > cache_.maxCost()) { return; }
    const QString absolute = QFileInfo(path).absoluteFilePath().toCaseFolded();
    cache_.remove(pathKeys_.take(absolute));
    cache_.insert(key, new QImage(image), nCostKiB);
    pathKeys_.insert(absolute, key);
    // 与 LRU 同步清理索引，避免长时间浏览导致路径索引无限增长。
    for (auto it = pathKeys_.begin(); it != pathKeys_.end();) {
        if (!cache_.contains(it.value())) { it = pathKeys_.erase(it); } else { ++it; }
    }
}

void ImageCache::clear()
{
    QMutexLocker locker(&mutex_);
    ++nRevision_;
    cache_.clear();
    pathKeys_.clear();
}

void ImageCache::remove(const QString& path)
{
    QMutexLocker locker(&mutex_);
    ++nRevision_;
    cache_.remove(pathKeys_.take(QFileInfo(path).absoluteFilePath().toCaseFolded()));
}

std::shared_ptr<ImageCache> ImageCache::shared()
{
    static const auto cache = std::make_shared<ImageCache>();
    return cache;
}

bool ImageCache::canPrefetch(const QSize& size)
{
    // 最坏按每像素 8 字节估算；未知尺寸不投机解码。
    return size.isValid() && static_cast<qint64>(size.width()) * size.height() <= 12LL * 1024 * 1024;
}

core::loader::LoadResult ImageCache::load(const QString& path, const util::Cancellation& canceled)
{
    if (util::canceled(canceled)) { return {}; }
    const QString key = keyForFile(path);
    std::shared_ptr<Flight> flight;
    QString flightKey;
    quint64 nRevision;
    {
        QMutexLocker locker(&mutex_);
        if (auto* image = cache_.object(key)) {
            ++nHitCount_;
            return { *image, QString() };
        }
        nRevision = nRevision_;
        flightKey = key + QString("|%1").arg(nRevision);
        flight = flights_.value(flightKey);
        if (flight) {
            while (!flight->bFinished && !util::canceled(canceled)) { flight->finished.wait(&mutex_, 25); }
            return util::canceled(canceled) ? core::loader::LoadResult() : flight->result;
        }
        flight = std::make_shared<Flight>();
        flights_.insert(flightKey, flight);
    }
    // 共享解码不能因一个请求取消而中断其他等待者；不捕获窗口对象。
    ++nDecodeCount_;
    const auto result = core::loader::loadImage(path);
    const bool bUnchanged = key == keyForFile(path);
    {
        QMutexLocker locker(&mutex_);
        if (result.ok() && bUnchanged && nRevision == nRevision_) {
            const qint64 nCost = (static_cast<qint64>(result.image.bytesPerLine()) * result.image.height() + 1023) / 1024;
            if (nCost <= cache_.maxCost()) {
                const QString absolute = QFileInfo(path).absoluteFilePath().toCaseFolded();
                cache_.remove(pathKeys_.take(absolute));
                cache_.insert(key, new QImage(result.image), static_cast<int>(nCost));
                pathKeys_.insert(absolute, key);
                for (auto it = pathKeys_.begin(); it != pathKeys_.end();) {
                    if (!cache_.contains(it.value())) { it = pathKeys_.erase(it); } else { ++it; }
                }
            }
        }
        flight->result = result;
        flight->bFinished = true;
        flight->finished.wakeAll();
        flights_.remove(flightKey);
    }
    return util::canceled(canceled) ? core::loader::LoadResult() : result;
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
