// 2026-09-06
// 功能：异步加载可见区域附近的缩略图并管理有界缓存。
// 目的：保证大目录中的缩略图导航响应与内存稳定。
#include "ThumbnailBar.h"

#include "core/cache/ThumbnailCache.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <mutex>

namespace {
constexpr int kThumbnailWidth = 112;
constexpr int kThumbnailHeight = 76;
constexpr int kItemWidth = 132;
constexpr int kCacheLimit = 256;
constexpr int kPrefetchItems = 12;
}

namespace ui {

ThumbnailBar::ThumbnailBar(QWidget* parent)
    : QListWidget(parent)
    , watcher_(new QFutureWatcher<QVector<ThumbnailResult>>(this))
    , loadTimer_(new QTimer(this))
{
    setObjectName(QString("ThumbnailBar"));
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setIconSize(QSize(kThumbnailWidth, kThumbnailHeight));
    setGridSize(QSize(kItemWidth, 106));
    setFixedHeight(122);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSpacing(4);

    loadTimer_->setSingleShot(true);
    loadTimer_->setInterval(40);
    connect(loadTimer_, &QTimer::timeout, this, &ThumbnailBar::startPendingLoad);
    connect(watcher_, &QFutureWatcher<QVector<ThumbnailResult>>::finished,
        this, &ThumbnailBar::onLoadFinished);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
        this, &ThumbnailBar::scheduleVisibleThumbnails);
    connect(this, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const int nIndex = row(item);
        if (nIndex >= 0 && nIndex < files_.size()) {
            emit fileActivated(files_.at(nIndex));
        }
    });
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int nIndex = row(item);
        if (nIndex >= 0 && nIndex < files_.size()) {
            emit fileActivated(files_.at(nIndex));
        }
    });
}

ThumbnailBar::~ThumbnailBar()
{
    ++nGeneration_;
    watcher_->waitForFinished();
}

void ThumbnailBar::setFiles(const QStringList& files, int nCurrentIndex)
{
    ++nGeneration_;
    files_ = files;
    iconCache_.clear();
    cacheOrder_.clear();
    clear();

    for (const QString& path : files_) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setToolTip(path);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        addItem(item);
    }
    setCurrentFileIndex(nCurrentIndex);
    scheduleVisibleThumbnails();
}

void ThumbnailBar::setCurrentFileIndex(int nIndex)
{
    if (nIndex < 0 || nIndex >= count()) {
        setCurrentRow(-1);
        return;
    }
    setCurrentRow(nIndex);
    scrollToItem(item(nIndex), QAbstractItemView::PositionAtCenter);
    scheduleVisibleThumbnails();
}

void ThumbnailBar::resizeEvent(QResizeEvent* event)
{
    QListWidget::resizeEvent(event);
    scheduleVisibleThumbnails();
}

void ThumbnailBar::scheduleVisibleThumbnails()
{
    bReloadPending_ = true;
    loadTimer_->start();
}

void ThumbnailBar::startPendingLoad()
{
    if (!bReloadPending_ || files_.isEmpty()) {
        return;
    }
    if (watcher_->isRunning()) {
        return;
    }
    bReloadPending_ = false;

    const int nFirstVisible = std::max(0,
        horizontalScrollBar()->value() / std::max(1, gridSize().width()) - kPrefetchItems);
    const int nVisibleCount = viewport()->width() / std::max(1, gridSize().width()) + 2;
    const int nLastVisible = std::min(files_.size() - 1,
        nFirstVisible + nVisibleCount + kPrefetchItems * 2);

    QVector<QPair<int, QString>> requests;
    for (int nIndex = nFirstVisible; nIndex <= nLastVisible; ++nIndex) {
        if (!iconCache_.contains(nIndex)) {
            requests.push_back(qMakePair(nIndex, files_.at(nIndex)));
        }
    }
    if (requests.isEmpty()) {
        return;
    }

    nRunningGeneration_ = nGeneration_;
    watcher_->setFuture(QtConcurrent::run([requests]() {
        static std::once_flag pruneFlag;
        std::call_once(pruneFlag, []() { core::cache::ThumbnailCache::prune(); });
        QVector<ThumbnailResult> results;
        results.reserve(requests.size());
        for (const auto& request : requests) {
            const QSize targetSize(kThumbnailWidth, kThumbnailHeight);
            QImage thumbnail = core::cache::ThumbnailCache::load(request.second, targetSize);
            QImageReader reader(request.second);
            if (thumbnail.isNull()) {
                const QSize originalSize = reader.size();
                if (originalSize.isValid()) {
                    reader.setScaledSize(originalSize.scaled(targetSize, Qt::KeepAspectRatio));
                }
                thumbnail = reader.read();
                core::cache::ThumbnailCache::store(request.second, targetSize, thumbnail);
            }
            ThumbnailResult result;
            result.nIndex = request.first;
            result.image = thumbnail;
            results.push_back(std::move(result));
        }
        return results;
    }));
}

void ThumbnailBar::onLoadFinished()
{
    const QVector<ThumbnailResult> results = watcher_->result();
    if (nRunningGeneration_ == nGeneration_) {
        for (const ThumbnailResult& result : results) {
            if (result.nIndex < 0 || result.nIndex >= count() || result.image.isNull()) {
                continue;
            }
            const QIcon icon(QPixmap::fromImage(result.image));
            item(result.nIndex)->setIcon(icon);
            cacheThumbnail(result.nIndex, icon);
        }
    }
    if (bReloadPending_) {
        loadTimer_->start();
    }
}

void ThumbnailBar::cacheThumbnail(int nIndex, const QIcon& icon)
{
    if (iconCache_.contains(nIndex)) {
        return;
    }
    iconCache_.insert(nIndex, icon);
    cacheOrder_.enqueue(nIndex);
    while (cacheOrder_.size() > kCacheLimit) {
        const int nOldIndex = cacheOrder_.dequeue();
        iconCache_.remove(nOldIndex);
        if (nOldIndex >= 0 && nOldIndex < count()) {
            item(nOldIndex)->setIcon(QIcon());
        }
    }
}

} // namespace ui
