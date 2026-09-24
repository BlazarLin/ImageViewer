// 2026-09-06
// 功能：提供线程安全、按内存成本淘汰的已解码图像缓存。
// 目的：支撑邻图预加载，降低连续翻图的解码等待且限制内存增长。
#pragma once

#include "core/loader/ImageLoader.h"
#include "util/BackgroundWork.h"
#include <QHash>
#include <QWaitCondition>
#include <QCache>
#include <QImage>
#include <QMutex>
#include <QString>

namespace core::cache {

class ImageCache {
public:
    explicit ImageCache(int nMaximumMebibytes = 384);

    bool find(const QString& path, QImage* image);
    void insert(const QString& path, const QImage& image);
    void clear();
    void remove(const QString& path);
    static std::shared_ptr<ImageCache> shared();
    core::loader::LoadResult load(const QString& path, const util::Cancellation& canceled = {});
    static bool canPrefetch(const QSize& size);
    quint64 decodeCount() const { return nDecodeCount_.load(); }
    quint64 hitCount() const { return nHitCount_.load(); }

    static QString keyForFile(const QString& path);

private:
    struct Flight {
        QWaitCondition finished;
        bool bFinished = false;
        core::loader::LoadResult result;
    };
    QHash<QString, std::shared_ptr<Flight>> flights_;
    QHash<QString, QString> pathKeys_;
    quint64 nRevision_ = 0;
    std::atomic<quint64> nDecodeCount_{0};
    std::atomic<quint64> nHitCount_{0};
    QCache<QString, QImage> cache_;
    QMutex mutex_;
};

} // namespace core::cache
