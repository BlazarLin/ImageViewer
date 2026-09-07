// 2026-09-06
// 功能：扫描、自然排序并导航当前目录的图像文件。
// 目的：将文件系统逻辑与图形界面分离。
#include "DirectoryModel.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>

#include <algorithm>

namespace core::navigation {

bool DirectoryModel::loadForFile(const QString& path, bool bRefresh)
{
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
        const QFileInfoList entries = QDir(directoryPath).entryInfoList(
            QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::NoSort);
        paths.reserve(entries.size());
        for (const QFileInfo& info : entries) {
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
        std::sort(paths.begin(), paths.end(), [&collator](const QString& left, const QString& right) {
            return collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
        });

        directoryPath_ = directoryPath;
        files_ = std::move(paths);
        nCurrentIndex_ = -1;
    }

    if (!bCurrentFileExists && bRefresh) {
        return true;
    }
    if (!setCurrentPath(currentInfo.absoluteFilePath())) {
        return loadForFile(path, true);
    }
    return true;
}

bool DirectoryModel::setCurrentPath(const QString& path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (int nIndex = 0; nIndex < files_.size(); ++nIndex) {
        if (QString::compare(files_.at(nIndex), absolutePath, Qt::CaseInsensitive) == 0) {
            nCurrentIndex_ = nIndex;
            return true;
        }
    }
    return false;
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
