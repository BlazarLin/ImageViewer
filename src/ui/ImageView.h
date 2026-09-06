#pragma once

#include <QGraphicsView>
#include <QImage>

namespace ui {

// 图像视图：负责缩放、平移、适配模式和高倍率像素网格。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    // 设置当前展示的图像。空图像清空场景。
    void setImage(const QImage& img);

    // 当前图像引用。
    const QImage& image() const { return current_; }

    double zoomFactor() const;

public slots:
    void fitToWindow();
    void fitToWidth();
    void fitToHeight();
    void actualSize();

signals:
    void zoomChanged(double factor);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    enum class ViewMode {
        Manual,
        FitWindow,
        FitWidth,
        FitHeight
    };

    void setupScene();
    void rebuildPixmapItem();
    void applyViewMode();
    void setZoomFactor(double factor, const QPoint& anchorPosition);
    void updateRenderMode();
    void notifyZoomChanged();

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QImage current_;
    ViewMode viewMode_ = ViewMode::FitWindow;
    double lastEmittedZoom_ = -1.0;
};

} // namespace ui
