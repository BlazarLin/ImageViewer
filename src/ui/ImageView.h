#pragma once

#include <QGraphicsView>
#include <QColor>
#include <QImage>
#include <vector>

class QAction;
class QToolButton;
class QGraphicsPixmapItem;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QPaintEvent;
template <typename T>
class QFutureWatcher;

namespace ui {

// 图像视图：负责缩放、平移、适配模式和高倍率像素网格。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);
    ~ImageView() override;

    // 设置当前展示的图像。空图像清空场景。
    void setImage(const QImage& img);
    void setImage(const QImage& img, bool bResetView);

    // 设置原图和处理图；对比启用时以同一坐标系分割展示。
    void setComparisonImages(const QImage& original, const QImage& processed,
        bool bEnabled);
    void setComparisonEnabled(bool bEnabled);
    bool pixelUsesOriginal(const QPoint& position) const;
    void setSelectionEnabled(bool bEnabled) { bSelectionEnabled_ = bEnabled; }
    double comparisonSplit() const { return dComparisonSplit_; }
    bool comparisonEnabled() const { return bComparisonEnabled_; }

    // 当前图像引用。
    const QImage& image() const { return current_; }

    void setNavigationActions(QAction* previous, QAction* next);
    double zoomFactor() const;
    QRect selectedRegion() const { return selectedRegion_; }

public slots:
    void fitToWindow();
    void fitToWidth();
    void fitToHeight();
    void actualSize();
    void clearSelection();

signals:
    void zoomChanged(double factor);
    void pixelHovered(const QPoint& position, const QColor& color, bool bValid);
    void roiSelected(const QRect& region);
    void fileDropped(const QString& path);
    void selectionCleared();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int nDx, int nDy) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    enum class ViewMode {
        Manual,
        FitWindow,
        FitWidth,
        FitHeight
    };

    void setupScene();
    void updateNavigationButtons();
    void rebuildPixmapItems();
    void updateHoverPixel(const QPoint& viewportPosition);
    void applyViewMode();
    void setZoomFactor(double factor, const QPoint& anchorPosition);
    void updateRenderMode();
    void notifyZoomChanged();
    void requestPyramid();
    void onPyramidFinished();
    void applyPyramidLevel();

    QToolButton* previousButton_ = nullptr;
    QToolButton* nextButton_ = nullptr;
    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QGraphicsPixmapItem* comparisonItem_ = nullptr;
    QImage current_;
    QImage original_;
    QImage processed_;
    std::vector<QImage> pyramid_;
    QFutureWatcher<std::vector<QImage>>* pyramidWatcher_ = nullptr;
    ViewMode viewMode_ = ViewMode::FitWindow;
    double lastEmittedZoom_ = -1.0;
    double dComparisonSplit_ = 0.5;
    QPointF roiStart_;
    QPointF roiEnd_;
    QRect selectedRegion_;
    bool bComparisonEnabled_ = false;
    bool bDraggingComparisonSplit_ = false;
    bool bSelectingRoi_ = false;
    bool bSelectionEnabled_ = true;
    quint64 nPyramidGeneration_ = 0;
    quint64 nRunningPyramidGeneration_ = 0;
    int nDisplayedPyramidLevel_ = -1;
    bool bPyramidPending_ = false;
};

} // namespace ui
