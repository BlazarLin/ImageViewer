// 2026-09-06
// 功能：实现 SHA-256 命名的 PNG 缩略图磁盘缓存及按时间清理。
// 目的：确保缓存可失效、可复用且空间占用受控。
#include "ThumbnailCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace core::cache {
namespace {
QMutex maintenanceMutex(QMutex::Recursive);
QString measuredDirectory;
qint64 nMeasuredBytes = -1;
int nWritesSinceScan = 0;
quint64 nMaintenanceScans = 0;
constexpr qint64 kMaximumBytes = 256LL * 1024 * 1024;
}

quint64 ThumbnailCache::maintenanceScanCount()
{
    QMutexLocker locker(&maintenanceMutex);
    return nMaintenanceScans;
}

QString ThumbnailCache::cacheDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QString("thumbnails"));
}

QString ThumbnailCache::keyForFile(const QString& sourcePath, const QSize& targetSize)
{
    const QFileInfo info(sourcePath);
    const QString canonical = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    const QByteArray identity = QString("exif-v2|%1|%2|%3|%4x%5")
        .arg(canonical).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch())
        .arg(targetSize.width()).arg(targetSize.height()).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(identity,
        QCryptographicHash::Sha256).toHex());
}

QImage ThumbnailCache::load(const QString& sourcePath, const QSize& targetSize)
{
    const QString path = QDir(cacheDirectory()).filePath(
        keyForFile(sourcePath, targetSize) + QString(".png"));
    QImageReader reader(path);
    return reader.read();
}

void ThumbnailCache::store(const QString& sourcePath, const QSize& targetSize,
    const QImage& thumbnail)
{
    if (thumbnail.isNull()) {
        return;
    }
    QMutexLocker locker(&maintenanceMutex);
    const QString directory = cacheDirectory();
    if (measuredDirectory != directory || nMeasuredBytes < 0) { prune(); }
    if (!QDir().mkpath(directory)) {
        return;
    }
    const QString path = QDir(directory).filePath(
        keyForFile(sourcePath, targetSize) + QString(".png"));
    const qint64 nPreviousBytes = QFileInfo(path).size();
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && thumbnail.save(&file, "PNG")
        && file.commit()) {
        nMeasuredBytes += QFileInfo(path).size() - nPreviousBytes;
        if (nMeasuredBytes > kMaximumBytes) { prune(kMaximumBytes * 9 / 10); }
        else if (++nWritesSinceScan >= 128) { prune(); }
    }
}

void ThumbnailCache::prune(qint64 nMaximumBytes)
{
    QMutexLocker locker(&maintenanceMutex);
    ++nMaintenanceScans;
    QDir directory(cacheDirectory());
    const QFileInfoList files = directory.entryInfoList(
        QStringList() << QString("*.png"), QDir::Files, QDir::Time | QDir::Reversed);
    qint64 nTotalBytes = 0;
    for (const QFileInfo& info : files) {
        nTotalBytes += info.size();
    }
    for (const QFileInfo& info : files) {
        if (nTotalBytes <= nMaximumBytes) {
            break;
        }
        const qint64 nSize = info.size();
        if (QFile::remove(info.absoluteFilePath())) {
            nTotalBytes -= nSize;
        }
    }
    measuredDirectory = directory.path();
    nMeasuredBytes = nTotalBytes;
    nWritesSinceScan = 0;
}

} // namespace core::cache
