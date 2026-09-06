#pragma once

#include <QGraphicsView>
#include <QImage>

namespace ui {

// M0 阶段:简单展示 QImage 的 QGraphicsView。
// 后续 M1/M3 在此扩展:鼠标滚轮以光标为锚缩放、空格拖拽、瓦片渲染。
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    // 设置当前展示的图像。空图像清空场景。
    void setImage(const QImage& img);

    // 当前图像引用。
    const QImage& image() const { return current_; }

private:
    void setupScene();
    void rebuildPixmapItem();

    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QImage current_;
};

} // namespace ui