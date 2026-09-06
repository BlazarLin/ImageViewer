// 2026-09-06
// 功能：管理当前图像所在目录的可浏览文件。
// 目的：为上一张/下一张和缩略图导航提供有界状态。
#pragma once

#include <QStringList>

namespace core::navigation {

class DirectoryModel {
public:
    bool loadForFile(const QString& path);
    bool setCurrentPath(const QString& path);
    bool movePrevious();
    bool moveNext();

    const QStringList& files() const { return files_; }
    QString currentPath() const;
    int currentIndex() const { return nCurrentIndex_; }
    int count() const { return files_.size(); }

private:
    QString directoryPath_;
    QStringList files_;
    int nCurrentIndex_ = -1;
};

} // namespace core::navigation
