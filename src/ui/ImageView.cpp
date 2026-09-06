#include "ImageView.h"

#include <QGraphicsPixmapItem>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ui {

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
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
    setBackgroundBrush(QColor(46, 48, 51));
    setFrameShape(QFrame::NoFrame);
}

void ImageView::setupScene()
{
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
}

void ImageView::setImage(const QImage& img)
{
    current_ = img;
    rebuildPixmapItem();
    if (!current_.isNull()) {
        scene_->setSceneRect(QRectF(current_.rect()));
    } else {
        scene_->setSceneRect(QRectF());
    }
    viewMode_ = ViewMode::FitWindow;
    applyViewMode();
}

void ImageView::rebuildPixmapItem()
{
    if (pixmapItem_) {
        scene_->removeItem(pixmapItem_);
        delete pixmapItem_;
        pixmapItem_ = nullptr;
    }
    if (!current_.isNull()) {
        pixmapItem_ = scene_->addPixmap(QPixmap::fromImage(current_));
        pixmapItem_->setTransformationMode(Qt::SmoothTransformation);
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
        if (viewMode_ == ViewMode::FitWindow || std::abs(zoomFactor() - 1.0) < 0.0001) {
            actualSize();
        } else {
            fitToWindow();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (current_.isNull() || zoomFactor() < 8.0) {
        return;
    }

    const QRectF imageRect(current_.rect());
    const QRectF visible = rect.intersected(imageRect);
    if (visible.isEmpty()) {
        return;
    }

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
    pixmapItem_->setTransformationMode(
        bShowPixels ? Qt::FastTransformation : Qt::SmoothTransformation);
    setRenderHint(QPainter::SmoothPixmapTransform, !bShowPixels);
    viewport()->update();
}

void ImageView::notifyZoomChanged()
{
    const double dZoom = zoomFactor();
    if (std::abs(dZoom - lastEmittedZoom_) < 0.00001) {
        return;
    }
    lastEmittedZoom_ = dZoom;
    emit zoomChanged(dZoom);
}

} // namespace ui
