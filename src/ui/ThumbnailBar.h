// 2026-09-06
// 功能：显示当前目录的水平缩略图导航栏。
// 目的：在不阻塞 UI 的前提下快速切换同目录图像。
#pragma once

#include <QHash>
#include <QListView>
#include "util/BackgroundWork.h"
#include <QQueue>
#include <QVector>

template <typename T>
class QFutureWatcher;
class QTimer;

namespace ui {

struct ThumbnailResult {
    int nIndex = -1;
    QImage image;
};

class ThumbnailListModel;

class ThumbnailBar : public QListView {
    Q_OBJECT
public:
    explicit ThumbnailBar(QWidget* parent = nullptr);
    ~ThumbnailBar() override;

    void setFiles(const QStringList& files, int nCurrentIndex);
    void setCurrentFileIndex(int nIndex);
    int count() const { return files_.size(); }
    int currentRow() const { return currentIndex().row(); }
    void invalidateFile(const QString& path);


signals:
    void fileActivated(const QString& path);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void scheduleVisibleThumbnails();
    void startPendingLoad();
    void onLoadFinished();

private:
    void cacheThumbnail(int nIndex, const QIcon& icon);

    friend class ThumbnailListModel;
    ThumbnailListModel* listModel_ = nullptr;
    util::Cancellation canceled_;
    QStringList files_;
    QHash<int, QIcon> iconCache_;
    QQueue<int> cacheOrder_;
    QFutureWatcher<QVector<ThumbnailResult>>* watcher_ = nullptr;
    QTimer* loadTimer_ = nullptr;
    quint64 nGeneration_ = 0;
    quint64 nRunningGeneration_ = 0;
    bool bReloadPending_ = false;
};

} // namespace ui
