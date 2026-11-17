// 2026-09-06
// 功能：扫描、自然排序并导航当前目录的图像文件。
// 目的：将文件系统逻辑与图形界面分离。
// 2026-12-11
// 变更：Linux 适配。QCollator 的 numericMode 依赖平台 ICU/locale，在部分
// Linux 环境（如 locale=C）下不生效，导致 img10 排在 img2 前；改为自带
// 确定性自然排序，数字段按数值、其余字符按码点忽略大小写，各平台结果一致。
#include "DirectoryModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>

#include <algorithm>

namespace core::navigation {

namespace {

// 自然排序比较：数字段按数值比较（img2 在 img10 前），其余字符忽略大小写
// 按码点比较；长度相同且逐字符相等的输入视为相等。返回 -1/0/1。
int compareNatural(const QString& left, const QString& right)
{
    int i = 0;
    int j = 0;
    const int nLeft = left.size();
    const int nRight = right.size();
    while (i < nLeft && j < nRight) {
        const bool bDigitLeft = left.at(i).isDigit();
        const bool bDigitRight = right.at(j).isDigit();
        if (bDigitLeft && bDigitRight) {
            int iEnd = i;
            int jEnd = j;
            while (iEnd < nLeft && left.at(iEnd).isDigit()) { ++iEnd; }
            while (jEnd < nRight && right.at(jEnd).isDigit()) { ++jEnd; }
            // 跳过前导零，剩余位数多者数值更大；位数相同则逐位比较。
            while (iEnd - i > 1 && left.at(i) == QLatin1Char('0')) { ++i; }
            while (jEnd - j > 1 && right.at(j) == QLatin1Char('0')) { ++j; }
            if (iEnd - i != jEnd - j) { return iEnd - i < jEnd - j ? -1 : 1; }
            for (int k = 0; i + k < iEnd; ++k) {
                if (left.at(i + k) != right.at(j + k)) {
                    return left.at(i + k).unicode() < right.at(j + k).unicode() ? -1 : 1;
                }
            }
            i = iEnd;
            j = jEnd;
        } else {
            const char16_t chLeft = left.at(i).toLower().unicode();
            const char16_t chRight = right.at(j).toLower().unicode();
            if (chLeft != chRight) { return chLeft < chRight ? -1 : 1; }
            ++i;
            ++j;
        }
    }
    if (i < nLeft) { return 1; }
    if (j < nRight) { return -1; }
    return 0;
}

} // namespace

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
        if (util::canceled(canceled)) { return false; }
        // 稳定排序保证等值键（如 img01 与 img1）保持扫描顺序相邻。
        std::stable_sort(paths.begin(), paths.end(), [](const QString& left, const QString& right) {
            return compareNatural(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
        });

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
