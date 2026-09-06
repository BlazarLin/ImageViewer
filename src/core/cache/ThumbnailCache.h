// 2026-09-06
// 功能：持久化缩略图并按源文件状态生成稳定缓存键。
// 目的：加速重复打开大目录，同时用磁盘硬上限避免无限累积。
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace core::cache {

class ThumbnailCache {
public:
    static QImage load(const QString& sourcePath, const QSize& targetSize);
    static void store(const QString& sourcePath, const QSize& targetSize,
        const QImage& thumbnail);
    static void prune(qint64 nMaximumBytes = 256LL * 1024 * 1024);
    static QString keyForFile(const QString& sourcePath, const QSize& targetSize);
    static QString cacheDirectory();
};

} // namespace core::cache
