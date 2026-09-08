// 2026-09-06
// 功能：显示当前目录的水平缩略图导航栏。
// 目的：在不阻塞 UI 的前提下快速切换同目录图像。
#pragma once

#include <QHash>
#include <QListWidget>
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

class ThumbnailBar : public QListWidget {
    Q_OBJECT
public:
    explicit ThumbnailBar(QWidget* parent = nullptr);
    ~ThumbnailBar() override;

    void setFiles(const QStringList& files, int nCurrentIndex);
    void setCurrentFileIndex(int nIndex);
    void setNameFilter(const QString& text);
    void setSizeLevel(int nLevel);
    int visibleFileCount() const { return visibleRows_.size(); }

signals:
    void visibleFilesChanged(int nCount);
    void fileActivated(const QString& path);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void scheduleVisibleThumbnails();
    void startPendingLoad();
    void onLoadFinished();

private:
    void cacheThumbnail(int nIndex, const QIcon& icon);

    QStringList files_;
    QString filter_;
    QVector<int> visibleRows_;
    QHash<int, QIcon> iconCache_;
    QQueue<int> cacheOrder_;
    QFutureWatcher<QVector<ThumbnailResult>>* watcher_ = nullptr;
    QTimer* loadTimer_ = nullptr;
    quint64 nGeneration_ = 0;
    quint64 nRunningGeneration_ = 0;
    bool bReloadPending_ = false;
};

} // namespace ui
