// 2026-09-06
// 功能：异步加载可见区域附近的缩略图并管理有界缓存。
// 目的：保证大目录中的缩略图导航响应与内存稳定。
#include "ThumbnailBar.h"

#include "core/cache/ThumbnailCache.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <mutex>

namespace {
constexpr int kThumbnailWidth = 112;
constexpr int kThumbnailHeight = 76;
constexpr int kItemWidth = 142;
constexpr int kItemHeight = 112;
constexpr int kCacheLimit = 256;
constexpr int kPrefetchItems = 4;
constexpr int kLoadBatchSize = 6;

class ThumbnailItemDelegate final : public QStyledItemDelegate {
public:
    explicit ThumbnailItemDelegate(QObject* parent)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(kItemWidth, kItemHeight);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        const bool bSelected = option.state.testFlag(QStyle::State_Selected);
        const bool bHovered = option.state.testFlag(QStyle::State_MouseOver);
        const QRect cardRect = option.rect.adjusted(3, 3, -3, -3);
        const QRect imageRect(cardRect.left() + 7, cardRect.top() + 5,
            cardRect.width() - 14, kThumbnailHeight + 4);
        const QRect textRect(cardRect.left() + 7, imageRect.bottom() + 4,
            cardRect.width() - 14, 20);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(bSelected ? QColor(45, 155, 211)
            : (bHovered ? QColor(77, 85, 95) : QColor(52, 57, 64)), bSelected ? 2 : 1));
        painter->setBrush(bSelected ? QColor(38, 63, 80) : QColor(39, 42, 47));
        painter->drawRoundedRect(cardRect, 6, 6);

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(29, 31, 35));
        painter->drawRoundedRect(imageRect, 4, 4);

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            const QSize pixmapSize = icon.actualSize(QSize(kThumbnailWidth, kThumbnailHeight));
            const QPixmap pixmap = icon.pixmap(pixmapSize, QIcon::Normal, QIcon::Off);
            const QPoint pixmapTopLeft(
                imageRect.center().x() - pixmap.width() / 2,
                imageRect.center().y() - pixmap.height() / 2);
            painter->drawPixmap(pixmapTopLeft, pixmap);
        }

        QFont textFont = option.font;
        textFont.setPointSize(9);
        painter->setFont(textFont);
        painter->setPen(bSelected ? QColor(255, 255, 255) : QColor(205, 211, 218));
        const QString fileName = index.data(Qt::DisplayRole).toString();
        const QString elidedName = QFontMetrics(textFont).elidedText(
            fileName, Qt::ElideMiddle, textRect.width());
        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, elidedName);
        painter->restore();
    }
};
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
    setMouseTracking(true);
    setIconSize(QSize(kThumbnailWidth, kThumbnailHeight));
    setGridSize(QSize(kItemWidth, kItemHeight));
    setFixedHeight(132);
    setItemDelegate(new ThumbnailItemDelegate(this));
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

    const int nVisibleFirst = std::max(0,
        horizontalScrollBar()->value() / std::max(1, gridSize().width()));
    const int nVisibleCount = viewport()->width() / std::max(1, gridSize().width()) + 2;
    const int nVisibleLast = std::min(files_.size() - 1, nVisibleFirst + nVisibleCount);
    const int nFirstLoad = std::max(0, nVisibleFirst - kPrefetchItems);
    const int nLastLoad = std::min(files_.size() - 1, nVisibleLast + kPrefetchItems);

    QVector<int> candidateIndices;
    candidateIndices.reserve(nLastLoad - nFirstLoad + 1);
    const auto appendCandidate = [this, &candidateIndices, nFirstLoad, nLastLoad](int nIndex) {
        if (nIndex >= nFirstLoad && nIndex <= nLastLoad
            && !iconCache_.contains(nIndex) && !candidateIndices.contains(nIndex)) {
            candidateIndices.push_back(nIndex);
        }
    };
    appendCandidate(currentRow());
    for (int nIndex = nVisibleFirst; nIndex <= nVisibleLast; ++nIndex) {
        appendCandidate(nIndex);
    }
    for (int nDistance = 1; nDistance <= kPrefetchItems; ++nDistance) {
        appendCandidate(nVisibleFirst - nDistance);
        appendCandidate(nVisibleLast + nDistance);
    }

    const int nBatchSize = iconCache_.isEmpty() ? 1 : kLoadBatchSize;
    QVector<QPair<int, QString>> requests;
    requests.reserve(nBatchSize);
    for (int nIndex : candidateIndices) {
        requests.push_back(qMakePair(nIndex, files_.at(nIndex)));
        if (requests.size() >= nBatchSize) {
            break;
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
            if (result.nIndex < 0 || result.nIndex >= count()) {
                continue;
            }
            const QIcon icon = result.image.isNull()
                ? QIcon() : QIcon(QPixmap::fromImage(result.image));
            if (!icon.isNull()) {
                item(result.nIndex)->setIcon(icon);
            }
            cacheThumbnail(result.nIndex, icon);
        }
    }
    bReloadPending_ = true;
    loadTimer_->start();
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
