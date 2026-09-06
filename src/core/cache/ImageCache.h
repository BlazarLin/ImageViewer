// 2026-09-06
// 功能：提供线程安全、按内存成本淘汰的已解码图像缓存。
// 目的：支撑邻图预加载，降低连续翻图的解码等待且限制内存增长。
#pragma once

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

    static QString keyForFile(const QString& path);

private:
    QCache<QString, QImage> cache_;
    QMutex mutex_;
};

} // namespace core::cache
