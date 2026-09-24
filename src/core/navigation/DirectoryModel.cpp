// 2026-09-06
// 功能：扫描、自然排序并导航当前目录的图像文件。
// 目的：将文件系统逻辑与图形界面分离。
#include "DirectoryModel.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>

#include <algorithm>
#include <vector>

namespace core::navigation {

void DirectoryModel::setFileList(const QStringList& paths, const QString& currentPath, const util::Cancellation& canceled)
{
    bExplicitFiles_ = paths.size() > 1;
    directoryPath_.clear();
    files_.clear();
    nCurrentIndex_ = -1;
    if (!bExplicitFiles_) { loadForFile(currentPath, false, canceled); return; }
    QSet<QString> seen;
    for (const QString& path : paths) {
        if (util::canceled(canceled)) { return; }
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (QFileInfo(absolute).isFile() && !seen.contains(absolute.toCaseFolded())) {
            seen.insert(absolute.toCaseFolded());
            files_.append(absolute);
        }
    }
    rebuildIndex();
    setCurrentPath(currentPath);
}

bool DirectoryModel::loadForFile(const QString& path, bool bRefresh, const util::Cancellation& canceled)
{
    if (util::canceled(canceled)) { return false; }
    if (bExplicitFiles_ && (bRefresh || indexOf(path) >= 0)) {
        if (bRefresh) {
            files_.erase(std::remove_if(files_.begin(), files_.end(), [](const QString& entry) {
                return !QFileInfo(entry).isFile();
            }), files_.end());
        }
        rebuildIndex();
        nCurrentIndex_ = -1;
        setCurrentPath(path);
        return true;
    }
    bExplicitFiles_ = false;
    const QFileInfo currentInfo(path);
    const bool bCurrentFileExists = currentInfo.exists() && currentInfo.isFile();
    if ((!bCurrentFileExists && !bRefresh) || !QDir(currentInfo.absolutePath()).exists()) {
        return false;
    }

    const QString directoryPath = currentInfo.absolutePath();
    if (bRefresh || directoryPath_ != directoryPath || files_.isEmpty()) {
        QSet<QString> suffixes;
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        for (const QByteArray& format : formats) {
            suffixes.insert(QString::fromLatin1(format).toLower());
        }
        const QStringList extraFormats = {
            QString("tif"), QString("tiff"), QString("webp"), QString("gif")
        };
        for (const QString& format : extraFormats) {
            suffixes.insert(format);
        }

        QStringList paths;
        QDirIterator entries(directoryPath, QDir::Files | QDir::Readable | QDir::NoSymLinks);
        while (entries.hasNext()) {
            if (util::canceled(canceled)) { return false; }
            entries.next();
            const QFileInfo info = entries.fileInfo();
            if (suffixes.contains(info.suffix().toLower())) {
                paths.push_back(info.absoluteFilePath());
            }
        }

        // 内容可解码但扩展名未知的当前文件，也应参与导航。
        if (bCurrentFileExists && !paths.contains(currentInfo.absoluteFilePath(), Qt::CaseInsensitive)) {
            paths.push_back(currentInfo.absoluteFilePath());
        }
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        struct Entry { QString path; QCollatorSortKey key; };
        std::vector<Entry> sorted;
        sorted.reserve(paths.size());
        for (const QString& entry : paths) {
            if (util::canceled(canceled)) { return false; }
            sorted.push_back({ entry, collator.sortKey(QFileInfo(entry).fileName()) });
        }
        std::sort(sorted.begin(), sorted.end(), [](const Entry& left, const Entry& right) {
            return left.key.compare(right.key) < 0;
        });
        if (util::canceled(canceled)) { return false; }
        paths.clear();
        for (const auto& entry : sorted) { paths.append(entry.path); }

        directoryPath_ = directoryPath;
        files_ = std::move(paths);
        rebuildIndex();
        nCurrentIndex_ = -1;
    }

    if (!bCurrentFileExists && bRefresh) {
        return true;
    }
    if (!setCurrentPath(currentInfo.absoluteFilePath())) {
        return loadForFile(path, true, canceled);
    }
    return true;
}

void DirectoryModel::rebuildIndex()
{
    indices_.clear();
    for (int nIndex = 0; nIndex < files_.size(); ++nIndex) { indices_.insert(files_.at(nIndex).toCaseFolded(), nIndex); }
}

int DirectoryModel::indexOf(const QString& path) const
{
    return indices_.value(QFileInfo(path).absoluteFilePath().toCaseFolded(), -1);
}

bool DirectoryModel::setCurrentPath(const QString& path)
{
    const int nIndex = indexOf(path);
    if (nIndex < 0) { return false; }
    nCurrentIndex_ = nIndex;
    return true;
}

bool DirectoryModel::movePrevious()
{
    if (nCurrentIndex_ <= 0) {
        return false;
    }
    --nCurrentIndex_;
    return true;
}

bool DirectoryModel::moveNext()
{
    if (nCurrentIndex_ < 0 || nCurrentIndex_ + 1 >= files_.size()) {
        return false;
    }
    ++nCurrentIndex_;
    return true;
}

QString DirectoryModel::currentPath() const
{
    if (nCurrentIndex_ < 0 || nCurrentIndex_ >= files_.size()) {
        return {};
    }
    return files_.at(nCurrentIndex_);
}

} // namespace core::navigation
