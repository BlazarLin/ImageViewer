// 2026-09-06
// 功能：管理当前图像所在目录的可浏览文件。
// 目的：为上一张/下一张和缩略图导航提供有界状态。
#pragma once

#include "util/BackgroundWork.h"
#include <QHash>
#include <QStringList>

namespace core::navigation {

class DirectoryModel {
public:
    void setFileList(const QStringList& paths, const QString& currentPath, const util::Cancellation& canceled = {});
    bool loadForFile(const QString& path, bool bRefresh = false, const util::Cancellation& canceled = {});
    bool setCurrentPath(const QString& path);
    int indexOf(const QString& path) const;
    bool movePrevious();
    bool moveNext();

    const QStringList& files() const { return files_; }
    QString currentPath() const;
    int currentIndex() const { return nCurrentIndex_; }
    int count() const { return files_.size(); }

private:
    void rebuildIndex();
    QHash<QString, int> indices_;
    bool bExplicitFiles_ = false;
    QString directoryPath_;
    QStringList files_;
    int nCurrentIndex_ = -1;
};

} // namespace core::navigation
