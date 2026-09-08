#include "ImageView.h"

#include "core/cache/ImagePyramid.h"

#include <QDragEnterEvent>
#include <QCursor>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QFutureWatcher>
#include <QMimeData>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

class ClippedPixmapItem final : public QGraphicsPixmapItem {
public:
    void setSplit(double dSplit)
    {
        dSplit_ = std::clamp(dSplit, 0.0, 1.0);
        update();
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override
    {
        painter->save();
        const QRectF bounds = boundingRect();
        const double dLeft = bounds.left() + bounds.width() * dSplit_;
        painter->setClipRect(QRectF(dLeft, bounds.top(), bounds.right() - dLeft, bounds.height()));
        QGraphicsPixmapItem::paint(painter, option, widget);
        painter->restore();
    }

private:
    double dSplit_ = 0.5;
};

QString firstLocalFilePath(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasUrls()) {
        return {};
    }
    for (const QUrl& url : mimeData->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && QFileInfo(path).isFile()) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

} // namespace

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
    , pyramidWatcher_(new QFutureWatcher<std::vector<QImage>>(this))
{
    setupScene();
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    const auto refreshPixel = [this](int) { updateHoverPixel(viewport()->mapFromGlobal(QCursor::pos())); };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, refreshPixel);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, refreshPixel);
    connect(pyramidWatcher_, &QFutureWatcher<std::vector<QImage>>::finished,
        this, &ImageView::onPyramidFinished);
}

ImageView::~ImageView()
{
    ++nPyramidGeneration_;
    pyramidWatcher_->waitForFinished();
}

void ImageView::setupScene()
{
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
}

void ImageView::setImage(const QImage& img)
{
    setImage(img, true);
}

void ImageView::setImage(const QImage& img, bool bResetView)
{
    const QRectF oldRect = scene_->sceneRect();
    clearSelection();
    current_ = img;
    original_ = QImage();
    processed_ = QImage();
    bComparisonEnabled_ = false;
    ++nPyramidGeneration_;
    pyramid_.clear();
    nDisplayedPyramidLevel_ = -1;
    rebuildPixmapItems();
    if (!current_.isNull()) {
        scene_->setSceneRect(QRectF(current_.rect()));
    } else {
        scene_->setSceneRect(QRectF());
    }
    requestPyramid();
    if (bResetView || oldRect.size() != scene_->sceneRect().size()) {
        viewMode_ = ViewMode::FitWindow;
        applyViewMode();
    } else {
        updateRenderMode();
        viewport()->update();
    }
    updateHoverPixel(viewport()->mapFromGlobal(QCursor::pos()));
}

void ImageView::setComparisonImages(const QImage& original, const QImage& processed,
    bool bEnabled)
{
    const QRectF oldRect = scene_->sceneRect();
    clearSelection();
    original_ = original;
    processed_ = processed;
    current_ = processed_.isNull() ? original_ : processed_;
    bComparisonEnabled_ = bEnabled && !original_.isNull() && !processed_.isNull()
        && original_.size() == processed_.size();
    ++nPyramidGeneration_;
    pyramid_.clear();
    nDisplayedPyramidLevel_ = -1;
    rebuildPixmapItems();
    scene_->setSceneRect(current_.isNull() ? QRectF() : QRectF(current_.rect()));
    if (oldRect.size() != scene_->sceneRect().size()) {
        viewMode_ = ViewMode::FitWindow;
        applyViewMode();
    } else {
        updateRenderMode();
        viewport()->update();
    }
    requestPyramid();
    updateHoverPixel(viewport()->mapFromGlobal(QCursor::pos()));
}

void ImageView::setComparisonEnabled(bool bEnabled)
{
    const bool bNext = bEnabled && !original_.isNull() && !processed_.isNull()
        && original_.size() == processed_.size();
    if (bComparisonEnabled_ == bNext) {
        return;
    }
    bComparisonEnabled_ = bNext;
    rebuildPixmapItems();
    updateRenderMode();
    viewport()->update();
    if (!bComparisonEnabled_) {
        requestPyramid();
    }
    updateHoverPixel(viewport()->mapFromGlobal(QCursor::pos()));
}

void ImageView::rebuildPixmapItems()
{
    if (comparisonItem_) {
        scene_->removeItem(comparisonItem_);
        delete comparisonItem_;
        comparisonItem_ = nullptr;
    }
    if (pixmapItem_) {
        scene_->removeItem(pixmapItem_);
        delete pixmapItem_;
        pixmapItem_ = nullptr;
    }
    if (!current_.isNull()) {
        const QImage& baseImage = bComparisonEnabled_ ? original_ : current_;
        pixmapItem_ = scene_->addPixmap(QPixmap::fromImage(baseImage));
        pixmapItem_->setTransformationMode(Qt::SmoothTransformation);
        pixmapItem_->setTransform(QTransform());
        if (bComparisonEnabled_) {
            auto* clippedItem = new ClippedPixmapItem();
            clippedItem->setPixmap(QPixmap::fromImage(processed_));
            clippedItem->setSplit(dComparisonSplit_);
            clippedItem->setTransformationMode(Qt::SmoothTransformation);
            scene_->addItem(clippedItem);
            comparisonItem_ = clippedItem;
        }
        nDisplayedPyramidLevel_ = 0;
    }
}

double ImageView::zoomFactor() const
{
    return transform().m11();
}

void ImageView::fitToWindow()
{
    viewMode_ = ViewMode::FitWindow;
    applyViewMode();
}

void ImageView::fitToWidth()
{
    viewMode_ = ViewMode::FitWidth;
    applyViewMode();
}

void ImageView::fitToHeight()
{
    viewMode_ = ViewMode::FitHeight;
    applyViewMode();
}

void ImageView::actualSize()
{
    if (current_.isNull()) {
        return;
    }
    viewMode_ = ViewMode::Manual;
    resetTransform();
    centerOn(scene_->sceneRect().center());
    updateRenderMode();
    notifyZoomChanged();
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    if (current_.isNull() || event->angleDelta().y() == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    viewMode_ = ViewMode::Manual;
    const double multiplier = std::pow(1.0015, event->angleDelta().y());
    setZoomFactor(zoomFactor() * multiplier, event->pos());
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (viewMode_ != ViewMode::Manual) {
        applyViewMode();
    }
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !current_.isNull()) {
        if (viewMode_ == ViewMode::FitWindow) {
            actualSize();
        } else {
            fitToWindow();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !current_.isNull()) {
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            clearSelection();
            bSelectingRoi_ = true;
            roiStart_ = mapToScene(event->pos());
            roiEnd_ = roiStart_;
            setDragMode(QGraphicsView::NoDrag);
            viewport()->update();
            event->accept();
            return;
        }
        if (bComparisonEnabled_) {
            const QPoint dividerPosition = mapFromScene(
                QPointF(current_.width() * dComparisonSplit_, current_.height() * 0.5));
            if (std::abs(event->pos().x() - dividerPosition.x()) <= 7) {
                bDraggingComparisonSplit_ = true;
                setDragMode(QGraphicsView::NoDrag);
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event)
{
    updateHoverPixel(event->pos());
    if (bSelectingRoi_) {
        roiEnd_ = mapToScene(event->pos());
        viewport()->update();
        event->accept();
        return;
    }
    if (bDraggingComparisonSplit_) {
        dComparisonSplit_ = std::clamp(mapToScene(event->pos()).x() / current_.width(), 0.0, 1.0);
        if (auto* clippedItem = dynamic_cast<ClippedPixmapItem*>(comparisonItem_)) {
            clippedItem->setSplit(dComparisonSplit_);
        }
        viewport()->update();
        updateHoverPixel(event->pos());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
    updateHoverPixel(event->pos());
}

void ImageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && bSelectingRoi_) {
        bSelectingRoi_ = false;
        setDragMode(QGraphicsView::ScrollHandDrag);
        roiEnd_ = mapToScene(event->pos());
        const QPoint topLeft(static_cast<int>(std::floor(std::min(roiStart_.x(), roiEnd_.x()))),
            static_cast<int>(std::floor(std::min(roiStart_.y(), roiEnd_.y()))));
        const QPoint bottomRight(std::max(topLeft.x(), static_cast<int>(std::ceil(std::max(roiStart_.x(), roiEnd_.x()))) - 1),
            std::max(topLeft.y(), static_cast<int>(std::ceil(std::max(roiStart_.y(), roiEnd_.y()))) - 1));
        const QRect region = QRect(topLeft, bottomRight).intersected(current_.rect());
        selectedRegion_ = region;
        viewport()->update();
        if (!region.isEmpty()) {
            emit roiSelected(region);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && bDraggingComparisonSplit_) {
        bDraggingComparisonSplit_ = false;
        setDragMode(QGraphicsView::ScrollHandDrag);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::clearSelection()
{
    selectedRegion_ = QRect();
    bSelectingRoi_ = false;
    bDraggingComparisonSplit_ = false;
    setDragMode(QGraphicsView::ScrollHandDrag);
    viewport()->update();
    emit selectionCleared();
}

void ImageView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        clearSelection();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void ImageView::leaveEvent(QEvent* event)
{
    emit pixelHovered(QPoint(), QColor(), false);
    QGraphicsView::leaveEvent(event);
}

void ImageView::dragEnterEvent(QDragEnterEvent* event)
{
    if (!firstLocalFilePath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragEnterEvent(event);
}

void ImageView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!firstLocalFilePath(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dragMoveEvent(event);
}

void ImageView::dropEvent(QDropEvent* event)
{
    const QString path = firstLocalFilePath(event->mimeData());
    if (!path.isEmpty()) {
        emit fileDropped(path);
        event->acceptProposedAction();
        return;
    }
    QGraphicsView::dropEvent(event);
}

void ImageView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
    if (!current_.isNull()) {
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect card = QRect(QPoint(), QSize(390, 156));
    QRect centeredCard = card;
    centeredCard.moveCenter(viewport()->rect().center());
    painter.setPen(QPen(QColor(78, 84, 92), 1, Qt::DashLine));
    painter.setBrush(QColor(41, 44, 49));
    painter.drawRoundedRect(centeredCard, 10, 10);

    QFont titleFont = painter.font();
    titleFont.setPointSize(12);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(226, 230, 235));
    painter.drawText(centeredCard.adjusted(20, 35, -20, -64),
        Qt::AlignCenter, tr("打开或拖放图像"));

    QFont hintFont = painter.font();
    hintFont.setPointSize(9);
    hintFont.setWeight(QFont::Normal);
    painter.setFont(hintFont);
    painter.setPen(QColor(151, 158, 168));
    painter.drawText(centeredCard.adjusted(20, 78, -20, -28),
        Qt::AlignCenter, tr("支持 PNG / JPEG / BMP / TIFF / WebP / GIF  ·  Ctrl+O"));
}

void ImageView::drawBackground(QPainter* painter, const QRectF& rect)
{
    Q_UNUSED(rect)
    static const QPixmap checkerPattern = []() {
        constexpr int nCellSize = 12;
        QPixmap pattern(nCellSize * 2, nCellSize * 2);
        pattern.fill(QColor(42, 44, 48));
        QPainter patternPainter(&pattern);
        patternPainter.fillRect(nCellSize, 0, nCellSize, nCellSize, QColor(52, 54, 59));
        patternPainter.fillRect(0, nCellSize, nCellSize, nCellSize, QColor(52, 54, 59));
        return pattern;
    }();

    painter->save();
    painter->resetTransform();
    painter->fillRect(viewport()->rect(), QBrush(checkerPattern));
    painter->restore();
}

void ImageView::updateHoverPixel(const QPoint& viewportPosition)
{
    if (current_.isNull() || !viewport()->rect().contains(viewportPosition)) {
        emit pixelHovered(QPoint(), QColor(), false);
        return;
    }
    const QPointF scenePosition = mapToScene(viewportPosition);
    const QPoint pixelPosition(static_cast<int>(std::floor(scenePosition.x())),
        static_cast<int>(std::floor(scenePosition.y())));
    const bool bValid = current_.rect().contains(pixelPosition);
    const QImage& sampledImage = bComparisonEnabled_
        && pixelPosition.x() < current_.width() * dComparisonSplit_ ? original_ : current_;
    emit pixelHovered(pixelPosition,
        bValid ? sampledImage.pixelColor(pixelPosition) : QColor(), bValid);
}

void ImageView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (current_.isNull() || zoomFactor() < 8.0) {
        // 对比线和 ROI 框仍需绘制。
    } else {
        const QRectF imageRect(current_.rect());
        const QRectF visible = rect.intersected(imageRect);
        if (!visible.isEmpty()) {
            const int nLeft = std::max(0, static_cast<int>(std::floor(visible.left())));
            const int nRight = std::min(current_.width(), static_cast<int>(std::ceil(visible.right())));
            const int nTop = std::max(0, static_cast<int>(std::floor(visible.top())));
            const int nBottom = std::min(current_.height(), static_cast<int>(std::ceil(visible.bottom())));

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            QPen pen(QColor(90, 90, 90, 150));
            pen.setCosmetic(true);
            painter->setPen(pen);
            for (int nX = nLeft; nX <= nRight; ++nX) {
                painter->drawLine(QPointF(nX, nTop), QPointF(nX, nBottom));
            }
            for (int nY = nTop; nY <= nBottom; ++nY) {
                painter->drawLine(QPointF(nLeft, nY), QPointF(nRight, nY));
            }
            painter->restore();
        }
    }

    if (bComparisonEnabled_) {
        painter->save();
        QPen dividerPen(QColor(255, 190, 55));
        dividerPen.setCosmetic(true);
        dividerPen.setWidth(2);
        painter->setPen(dividerPen);
        const double dX = current_.width() * dComparisonSplit_;
        painter->drawLine(QPointF(dX, 0), QPointF(dX, current_.height()));
        painter->restore();
    }
    if (bSelectingRoi_ || !selectedRegion_.isEmpty()) {
        painter->save();
        QPen roiPen(QColor(76, 190, 255));
        roiPen.setCosmetic(true);
        roiPen.setWidth(2);
        painter->setPen(roiPen);
        painter->setBrush(QColor(76, 190, 255, 35));
        painter->drawRect(bSelectingRoi_
            ? QRectF(roiStart_, roiEnd_).normalized().intersected(QRectF(current_.rect()))
            : QRectF(selectedRegion_));
        painter->restore();
    }
}

void ImageView::applyViewMode()
{
    if (current_.isNull() || viewport()->width() <= 0 || viewport()->height() <= 0) {
        resetTransform();
        notifyZoomChanged();
        return;
    }

    resetTransform();
    const QRectF bounds = scene_->sceneRect();
    const double dWidthScale = viewport()->width() / bounds.width();
    const double dHeightScale = viewport()->height() / bounds.height();
    double dScale = 1.0;
    switch (viewMode_) {
    case ViewMode::FitWindow:
        dScale = std::min(dWidthScale, dHeightScale);
        break;
    case ViewMode::FitWidth:
        dScale = dWidthScale;
        break;
    case ViewMode::FitHeight:
        dScale = dHeightScale;
        break;
    case ViewMode::Manual:
        return;
    }
    dScale = std::clamp(dScale, 0.01, 64.0);
    scale(dScale, dScale);
    centerOn(bounds.center());
    updateRenderMode();
    notifyZoomChanged();
}

void ImageView::setZoomFactor(double factor, const QPoint& anchorPosition)
{
    const double dNewFactor = std::clamp(factor, 0.01, 64.0);
    const QPointF sceneAnchor = mapToScene(anchorPosition);

    QTransform next;
    next.scale(dNewFactor, dNewFactor);
    setTransform(next);

    const QPoint currentViewportPosition = mapFromScene(sceneAnchor);
    const QPoint viewportDelta = currentViewportPosition - anchorPosition;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + viewportDelta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + viewportDelta.y());

    updateRenderMode();
    notifyZoomChanged();
}

void ImageView::updateRenderMode()
{
    if (!pixmapItem_) {
        return;
    }
    const bool bShowPixels = zoomFactor() >= 8.0;
    applyPyramidLevel();
    pixmapItem_->setTransformationMode(
        bShowPixels ? Qt::FastTransformation : Qt::SmoothTransformation);
    if (comparisonItem_) {
        comparisonItem_->setTransformationMode(
            bShowPixels ? Qt::FastTransformation : Qt::SmoothTransformation);
    }
    setRenderHint(QPainter::SmoothPixmapTransform, !bShowPixels);
    viewport()->update();
}

void ImageView::requestPyramid()
{
    if (current_.isNull() || bComparisonEnabled_
        || std::max(current_.width(), current_.height()) <= 2048) {
        return;
    }
    if (pyramidWatcher_->isRunning()) {
        bPyramidPending_ = true;
        return;
    }
    bPyramidPending_ = false;
    nRunningPyramidGeneration_ = nPyramidGeneration_;
    const QImage source = current_;
    pyramidWatcher_->setFuture(QtConcurrent::run([source]() {
        return core::cache::ImagePyramid::build(source);
    }));
}

void ImageView::onPyramidFinished()
{
    if (nRunningPyramidGeneration_ == nPyramidGeneration_) {
        pyramid_ = pyramidWatcher_->result();
        nDisplayedPyramidLevel_ = -1;
        applyPyramidLevel();
    }
    if (bPyramidPending_) {
        requestPyramid();
    }
}

void ImageView::applyPyramidLevel()
{
    if (!pixmapItem_ || bComparisonEnabled_ || pyramid_.empty()) {
        return;
    }
    int nLevel = 0;
    const double dZoom = zoomFactor();
    if (dZoom < 1.0) {
        nLevel = static_cast<int>(std::floor(std::log2(1.0 / std::max(0.0001, dZoom))));
        nLevel = std::clamp(nLevel, 0, static_cast<int>(pyramid_.size()) - 1);
    }
    if (dZoom >= 8.0) {
        nLevel = 0;
    }
    if (nLevel == nDisplayedPyramidLevel_) {
        return;
    }
    pixmapItem_->setPixmap(QPixmap::fromImage(pyramid_.at(nLevel)));
    const QImage& levelImage = pyramid_.at(nLevel);
    pixmapItem_->setTransform(QTransform::fromScale(
        static_cast<double>(current_.width()) / levelImage.width(),
        static_cast<double>(current_.height()) / levelImage.height()));
    nDisplayedPyramidLevel_ = nLevel;
}

void ImageView::notifyZoomChanged()
{
    updateHoverPixel(viewport()->mapFromGlobal(QCursor::pos()));
    const double dZoom = zoomFactor();
    if (std::abs(dZoom - lastEmittedZoom_) < 0.00001) {
        return;
    }
    lastEmittedZoom_ = dZoom;
    emit zoomChanged(dZoom);
}

} // namespace ui
