#include "ImageView.h"

#include <QGraphicsPixmapItem>
#include <QResizeEvent>

namespace ui {

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
{
    setupScene();
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setBackgroundBrush(QColor(0x20, 0x20, 0x20));
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
    // M0:简单 fitInView。后续 M1 改为保持缩放状态。
    if (!current_.isNull()) {
        fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    }
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

} // namespace ui