#include "ImageView.h"

#include "core/cache/ImagePyramid.h"

#include <QDragEnterEvent>
#include <QCursor>
#include <QAction>
#include <QToolButton>
#include <QLabel>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QFutureWatcher>
#include <QMimeData>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
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
    , overviewWatcher_(new QFutureWatcher<QImage>(this))
{
    setupScene();
    previousButton_ = new QToolButton(viewport());
    nextButton_ = new QToolButton(viewport());
    previousButton_->setObjectName(QString("PreviousImageButton"));
    nextButton_->setObjectName(QString("NextImageButton"));
    for (QToolButton* button : { previousButton_, nextButton_ }) {
        button->setFixedSize(48, 64);
        button->setIconSize(QSize(28, 28));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QString(
            "QToolButton { background:rgba(25,29,35,190); border:1px solid #56616c; border-radius:12px; }"
            "QToolButton:hover { background:#176b98; border-color:#45afe2; }"
            "QToolButton:pressed { background:#125374; }"
            "QToolButton:disabled { background:rgba(25,29,35,85); border-color:#34383e; }"));
        button->hide();
    }
    pixelHint_ = new QLabel(viewport());
    pixelHint_->setObjectName(QString("CursorPixelHint"));
    pixelHint_->setAttribute(Qt::WA_TransparentForMouseEvents);
    pixelHint_->setStyleSheet(QString("QLabel { color:#f4f7fa; background:rgba(24,28,34,235); border:1px solid #647586; border-radius:5px; padding:5px 8px; }"));
    pixelHint_->hide();
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
    connect(overviewWatcher_, &QFutureWatcher<QImage>::finished, this, [this]() {
        if (nRunningOverviewGeneration_ == nOverviewGeneration_) {
            overviewImage_ = overviewWatcher_->result();
            if (!navigationOverviewRect().isEmpty()) { viewport()->update(navigationOverviewRect()); }
        } else {
            requestNavigationOverview();
        }
    });
}

ImageView::~ImageView()
{
    util::cancel(pyramidCanceled_);
    util::cancel(overviewCanceled_);
    ++nPyramidGeneration_;
    ++nOverviewGeneration_;
}

void ImageView::setNavigationActions(QAction* previous, QAction* next)
{
    const auto arrowIcon = [](bool bLeft) {
        QIcon icon;
        for (int nSize : { 28, 56 }) {
            for (bool bDisabled : { false, true }) {
                QPixmap pixmap(nSize, nSize);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.scale(nSize / 28.0, nSize / 28.0);
                QPen pen(bDisabled ? QColor(100, 110, 120) : QColor(238, 246, 252));
                pen.setWidthF(2.8);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                QPainterPath path;
                path.moveTo(bLeft ? 17 : 11, 6);
                path.lineTo(bLeft ? 9 : 19, 14);
                path.lineTo(bLeft ? 17 : 11, 22);
                painter.drawPath(path);
                painter.end();
                icon.addPixmap(pixmap, bDisabled ? QIcon::Disabled : QIcon::Normal);
            }
        }
        return icon;
    };
    previous->setIcon(arrowIcon(true));
    next->setIcon(arrowIcon(false));
    previousButton_->setDefaultAction(previous);
    nextButton_->setDefaultAction(next);
    previousButton_->setAccessibleName(previous->text());
    nextButton_->setAccessibleName(next->text());
    updateNavigationButtons();
}

void ImageView::setChromeActions(QAction* toolbar)
{
    toolbarButton_ = new QToolButton(viewport());
    toolbarButton_->setObjectName(QString("ExpandToolbarButton"));
    toolbarButton_->setDefaultAction(toolbar);
    toolbarButton_->setFixedSize(34, 28);
    toolbarButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbarButton_->setFocusPolicy(Qt::NoFocus);
    toolbarButton_->setCursor(Qt::PointingHandCursor);
    toolbarButton_->setStyleSheet(QString("QToolButton { color:#dfe3e8; background:#303840; border:1px solid #647586; border-radius:5px; } QToolButton:hover { background:#176b98; }"));
    connect(toolbar, &QAction::toggled, this, [this](bool) { updateNavigationButtons(); });
    updateNavigationButtons();
}

void ImageView::setMinimalMode(bool bMinimal)
{
    bMinimalMode_ = bMinimal;
    setHorizontalScrollBarPolicy(bMinimal ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(bMinimal ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    updateNavigationButtons();
}

void ImageView::updateNavigationButtons()
{
    if (!previousButton_ || !nextButton_) {
        return;
    }
    if (toolbarButton_) {
        toolbarButton_->setVisible(!bMinimalMode_ && !toolbarButton_->defaultAction()->isChecked());
        toolbarButton_->setArrowType(Qt::DownArrow);
        toolbarButton_->move(std::max(0, viewport()->width() - 42), 6);
        toolbarButton_->raise();
    }
    const bool bVisible = !bMinimalMode_ && !current_.isNull() && previousButton_->defaultAction();
    previousButton_->setVisible(bVisible);
    nextButton_->setVisible(bVisible);
    const int nY = std::max(0, (viewport()->height() - previousButton_->height()) / 2);
    previousButton_->move(12, nY);
    nextButton_->move(std::max(12, viewport()->width() - nextButton_->width() - 12), nY);
    previousButton_->raise();
    nextButton_->raise();
}

void ImageView::scrollContentsBy(int nDx, int nDy)
{
    QGraphicsView::scrollContentsBy(nDx, nDy);
    // 棋盘格固定在视口坐标。仅修复露出的背景区域，保留不透明图像的滚动复用。
    QRegion background(viewport()->rect());
    if (!current_.isNull() && !current_.hasAlphaChannel()
        && (!bComparisonEnabled_ || !original_.hasAlphaChannel())) {
        background -= mapFromScene(QRectF(current_.rect())).boundingRect().adjusted(2, 2, -2, -2);
    }
    if (!background.isEmpty()) { viewport()->update(background); }
    updateNavigationButtons();
    refreshLoupe();
    if (!navigationOverviewRect().isEmpty()) { viewport()->update(navigationOverviewRect()); }
}

void ImageView::setupScene()
{
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
}

void ImageView::setImage(const QImage& img)
{
    // 切换图像默认保留当前缩放比，避免翻图时重新适配打断浏览节奏。
    setImage(img, false);
}

void ImageView::setImage(const QImage& img, bool bResetView)
{
    const QRectF oldRect = scene_->sceneRect();
    // 手动缩放时记录绝对图像坐标。不同尺寸的图片不能按相对百分比恢复，
    // 否则同一像素位置会随宽高比变化而产生“图像被移动”的跳动。
    const QPointF oldImageCenter = mapToScene(viewport()->rect().center());
    const bool bRestoreImageCenter = !oldRect.isEmpty() && viewMode_ == ViewMode::Manual;
    clearSelection();
    current_ = img;
    bAwaitingFirstFrame_ = !img.isNull();
    updateNavigationButtons();
    original_ = QImage();
    processed_ = QImage();
    bComparisonEnabled_ = false;
    requestNavigationOverview();
    util::cancel(pyramidCanceled_);
    ++nPyramidGeneration_;
    pyramid_.clear();
    nDisplayedPyramidLevel_ = -1;
    rebuildPixmapItems();
    scene_->setSceneRect(current_.isNull() ? QRectF() : QRectF(current_.rect()));
    requestPyramid();
    if (bResetView) {
        viewMode_ = ViewMode::FitWindow;
        applyViewMode();
    } else if (viewMode_ != ViewMode::Manual) {
        applyViewMode();
    } else {
        const QRectF bounds = scene_->sceneRect();
        const QPointF center = bRestoreImageCenter
            ? QPointF(std::clamp(oldImageCenter.x(), bounds.left(), bounds.right()),
                std::clamp(oldImageCenter.y(), bounds.top(), bounds.bottom()))
            : bounds.center();
        centerOn(center);
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
    updateNavigationButtons();
    bComparisonEnabled_ = bEnabled && !original_.isNull() && !processed_.isNull()
        && original_.size() == processed_.size();
    requestNavigationOverview();
    util::cancel(pyramidCanceled_);
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
    updateNavigationButtons();
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
        if (bSelectionEnabled_ && event->modifiers().testFlag(Qt::ShiftModifier)) {
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
    if (bLoupeEnabled_) {
        viewport()->update();
    }
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
    // 首尾禁用翻图动作后仍吞掉方向键，避免 QGraphicsView 将它解释为平移。
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        auto* button = event->key() == Qt::Key_Left ? previousButton_ : nextButton_;
        if (event->modifiers() == Qt::NoModifier && button && button->defaultAction()
            && button->defaultAction()->isEnabled()) {
            button->defaultAction()->trigger();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        clearSelection();
        if (window() && window()->isFullScreen()) {
            if (auto* action = window()->findChild<QAction*>(QString("ToggleFullScreen"))) {
                action->setChecked(false);
            } else {
                window()->showNormal();
            }
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void ImageView::leaveEvent(QEvent* event)
{
    pixelHint_->hide();
    emit pixelHovered(QPoint(), QColor(), false);
    refreshLoupe();
    QGraphicsView::leaveEvent(event);
}

bool ImageView::viewportEvent(QEvent* event)
{
    const bool bHandled = QGraphicsView::viewportEvent(event);
    if (event->type() == QEvent::Leave && pixelHint_) {
        // 从图像区进入工具按钮/滚动条时不一定触发外层视图的 leaveEvent。
        pixelHint_->hide();
        emit pixelHovered(QPoint(), QColor(), false);
    }
    return bHandled;
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
    if (bAwaitingFirstFrame_) { bAwaitingFirstFrame_ = false; emit firstFramePainted(); }
    if (!current_.isNull()) {
        QPainter overlayPainter(viewport());
        if (bLoupeEnabled_) { drawLoupe(overlayPainter); }
        drawNavigationOverview(overlayPainter);
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect card = QRect(QPoint(), QSize(std::min(520, viewport()->width() - 32), 180));
    QRect centeredCard = card;
    centeredCard.moveCenter(viewport()->rect().center());
    painter.setPen(QPen(QColor(78, 84, 92), 1, Qt::DashLine));
    painter.setBrush(QColor(41, 44, 49));
    painter.drawRoundedRect(centeredCard, 10, 10);

    QFont titleFont = painter.font();
    titleFont.setPixelSize(15);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(226, 230, 235));
    painter.drawText(centeredCard.adjusted(20, 35, -20, -64),
        Qt::AlignCenter, tr("打开或拖放图像"));

    QFont hintFont = painter.font();
    hintFont.setPixelSize(12);
    hintFont.setWeight(QFont::Normal);
    painter.setFont(hintFont);
    painter.setPen(QColor(151, 158, 168));
    painter.drawText(centeredCard.adjusted(20, 78, -20, -28),
        Qt::AlignCenter | Qt::TextWordWrap, tr("支持 PNG / JPEG / BMP / TIFF / WebP / GIF  ·  Ctrl+O"));
}

void ImageView::refreshLoupe()
{
    if (bLoupeEnabled_) {
        viewport()->update();
    }
}

void ImageView::drawLoupe(QPainter& painter)
{
    const QPoint pos = viewport()->mapFromGlobal(QCursor::pos());
    if (!viewport()->rect().contains(pos)) {
        return;
    }
    const QPointF scenePosition = mapToScene(pos);
    const QPoint pixel(static_cast<int>(std::floor(scenePosition.x())),
        static_cast<int>(std::floor(scenePosition.y())));
    if (!current_.rect().contains(pixel)) {
        return;
    }

    constexpr int kLoupePixels = 20;
    constexpr int kLoupeScale = 8;
    constexpr int kLoupeSize = kLoupePixels * kLoupeScale;
    constexpr int kMargin = 12;

    const QRect source(pixel.x() - kLoupePixels / 2, pixel.y() - kLoupePixels / 2,
        kLoupePixels, kLoupePixels);
    const QRect clipped = source.intersected(current_.rect());

    QPoint topLeft(pos.x() + 24, pos.y() + 24);
    if (topLeft.x() + kLoupeSize > viewport()->width() - kMargin) {
        topLeft.setX(pos.x() - 24 - kLoupeSize);
    }
    if (topLeft.y() + kLoupeSize > viewport()->height() - kMargin) {
        topLeft.setY(pos.y() - 24 - kLoupeSize);
    }
    topLeft.setX(std::clamp(topLeft.x(), kMargin,
        std::max(kMargin, viewport()->width() - kLoupeSize - kMargin)));
    topLeft.setY(std::clamp(topLeft.y(), kMargin,
        std::max(kMargin, viewport()->height() - kLoupeSize - kMargin)));

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QRectF target(topLeft, QSize(kLoupeSize, kLoupeSize));
    painter.fillRect(target, QColor(24, 26, 30));
    if (!clipped.isEmpty()) {
        const QRectF clippedTarget(topLeft.x() + (clipped.left() - source.left()) * kLoupeScale,
            topLeft.y() + (clipped.top() - source.top()) * kLoupeScale,
            clipped.width() * kLoupeScale, clipped.height() * kLoupeScale);
        // 对比模式下与像素取样一致：分割线左侧显示原图。
        const QImage& sample = pixelUsesOriginal(pixel) && !original_.isNull() ? original_ : current_;
        painter.drawImage(clippedTarget, sample, QRectF(clipped));
    }

    // 中心十字与当前像素框。
    const QPointF center = target.center();
    QPen crossPen(QColor(255, 255, 255, 190), 1);
    crossPen.setCosmetic(true);
    painter.setPen(crossPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(center.x() - 14, center.y()), QPointF(center.x() - 5, center.y()));
    painter.drawLine(QPointF(center.x() + 5, center.y()), QPointF(center.x() + 14, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - 14), QPointF(center.x(), center.y() - 5));
    painter.drawLine(QPointF(center.x(), center.y() + 5), QPointF(center.x(), center.y() + 14));
    painter.setPen(QPen(QColor(76, 190, 255), 1));
    painter.drawRect(QRectF(center.x() - kLoupeScale / 2.0, center.y() - kLoupeScale / 2.0,
        kLoupeScale, kLoupeScale));

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(45, 155, 211), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(target.adjusted(-1, -1, 1, 1), 6, 6);
    painter.restore();
}

QRect ImageView::navigationOverviewRect() const
{
    if (current_.isNull() || zoomFactor() <= 1.0 || viewport()->width() < 100
        || viewport()->height() < 100) {
        return {};
    }
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect().intersected(QRectF(current_.rect()));
    if (visible.contains(QRectF(current_.rect()))) { return {}; }
    const QSize size = current_.size().scaled(QSize(168, 120), Qt::KeepAspectRatio);
    return QRect(QPoint(12, std::max(0, viewport()->height() - size.height() - 12)), size);
}

void ImageView::drawNavigationOverview(QPainter& painter)
{
    const QRect target = navigationOverviewRect();
    if (target.isEmpty()) { return; }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(target.adjusted(-5, -5, 5, 5), QColor(20, 24, 29, 225));
    if (!overviewImage_.isNull()) { painter.drawImage(target, overviewImage_); }
    painter.setPen(QPen(QColor(125, 143, 158), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(target);

    const QRectF visible = mapToScene(viewport()->rect()).boundingRect().intersected(QRectF(current_.rect()));
    if (!visible.isEmpty()) {
        const qreal dX = target.width() / static_cast<qreal>(current_.width());
        const qreal dY = target.height() / static_cast<qreal>(current_.height());
        QRectF region(target.left() + visible.left() * dX,
            target.top() + visible.top() * dY,
            visible.width() * dX, visible.height() * dY);
        painter.fillRect(region, QColor(45, 155, 211, 35));
        painter.setPen(QPen(QColor(76, 190, 255), 2));
        painter.drawRect(region);
    }
    painter.restore();
}

void ImageView::requestNavigationOverview()
{
    util::cancel(overviewCanceled_);
    ++nOverviewGeneration_;
    overviewImage_ = QImage();
    // 若上一任务尚未结束，完成信号会按最新代数重新请求。
    if (current_.isNull() || overviewWatcher_->isRunning()) { return; }
    nRunningOverviewGeneration_ = nOverviewGeneration_;
    overviewCanceled_ = std::make_shared<std::atomic_bool>(false);
    const QImage source = current_;
    overviewWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Compute), [source]() {
        return source.size().width() > 168 || source.size().height() > 120
            ? source.scaled(QSize(168, 120), Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : source;
    }));
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

bool ImageView::pixelUsesOriginal(const QPoint& position) const
{
    return processed_.isNull() || (bComparisonEnabled_
        && position.x() < current_.width() * dComparisonSplit_);
}

void ImageView::updateHoverPixel(const QPoint& viewportPosition)
{
    if (current_.isNull() || !viewport()->rect().contains(viewportPosition)) {
        if (pixelHint_) { pixelHint_->hide(); }
        emit pixelHovered(QPoint(), QColor(), false);
        return;
    }
    const QPointF scenePosition = mapToScene(viewportPosition);
    const QPoint pixelPosition(static_cast<int>(std::floor(scenePosition.x())),
        static_cast<int>(std::floor(scenePosition.y())));
    const bool bValid = current_.rect().contains(pixelPosition);
    const QImage& sampledImage = pixelUsesOriginal(pixelPosition) && !original_.isNull() ? original_ : current_;
    const QColor color = bValid ? sampledImage.pixelColor(pixelPosition) : QColor();
    emit pixelHovered(pixelPosition, color, bValid);
    if (!pixelHint_) { return; }
    if (!bValid || zoomFactor() < 8.0) {
        pixelHint_->hide();
        return;
    }
    // 仅检查格式/最多 256 项的调色板，不在鼠标热路径扫描整图。
    const bool bGray = sampledImage.format() == QImage::Format_Grayscale8
        || sampledImage.format() == QImage::Format_Grayscale16
        || (sampledImage.depth() <= 8 && sampledImage.isGrayscale());
    const QString value = bGray ? tr("灰度: %1").arg(color.red())
        : tr("BGR: %1, %2, %3").arg(color.blue()).arg(color.green()).arg(color.red());
    const QString text = tr("X: %1  Y: %2\n%3").arg(pixelPosition.x()).arg(pixelPosition.y()).arg(value)
        + (util::highDepth(sampledImage) ? tr("\n8 位显示值（非原始值）") : QString());
    if (pixelHint_->text() != text) { pixelHint_->setText(text); pixelHint_->adjustSize(); }
    QPoint position = viewportPosition + QPoint(18, 22);
    if (position.x() + pixelHint_->width() > viewport()->width()) {
        position.setX(viewportPosition.x() - pixelHint_->width() - 18);
    }
    if (position.y() + pixelHint_->height() > viewport()->height()) {
        position.setY(viewportPosition.y() - pixelHint_->height() - 18);
    }
    position.setX(std::max(0, position.x()));
    position.setY(std::max(0, position.y()));
    pixelHint_->move(position);
    pixelHint_->show();
    pixelHint_->raise();
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
    if (pyramid_.empty() && !pyramidWatcher_->isRunning()) { requestPyramid(); }
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
    if (current_.isNull() || bComparisonEnabled_ || zoomFactor() >= 1.0
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
    pyramidCanceled_ = std::make_shared<std::atomic_bool>(false);
    const auto canceled = pyramidCanceled_;
    pyramidWatcher_->setFuture(QtConcurrent::run(util::workPool(util::WorkPool::Compute), [source, canceled]() {
        return core::cache::ImagePyramid::build(source, 1024, 8, canceled);
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
