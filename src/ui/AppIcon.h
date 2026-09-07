// 2026-09-07
// 功能：生成多尺寸图像查看器应用图标。
// 目的：替代文字占位图标，并统一标题栏、窗口和任务栏标识。
#pragma once

#include <algorithm>

#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace ui {

inline QIcon createAppIcon()
{
    QIcon icon;
    const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
    for (int nSize : sizes) {
        QPixmap pixmap(nSize, nSize);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal dScale = static_cast<qreal>(nSize);
        const QRectF outer(0.04 * dScale, 0.04 * dScale, 0.92 * dScale, 0.92 * dScale);
        QLinearGradient gradient(outer.topLeft(), outer.bottomRight());
        gradient.setColorAt(0.0, QColor(38, 169, 218));
        gradient.setColorAt(1.0, QColor(20, 93, 157));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(outer, 0.2 * dScale, 0.2 * dScale);

        QPen framePen(QColor(244, 249, 252));
        framePen.setWidthF(std::max<qreal>(1.2, 0.055 * dScale));
        framePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(framePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(0.2 * dScale, 0.22 * dScale,
            0.54 * dScale, 0.46 * dScale), 0.06 * dScale, 0.06 * dScale);

        QPainterPath mountains;
        mountains.moveTo(0.24 * dScale, 0.61 * dScale);
        mountains.lineTo(0.38 * dScale, 0.45 * dScale);
        mountains.lineTo(0.48 * dScale, 0.55 * dScale);
        mountains.lineTo(0.58 * dScale, 0.39 * dScale);
        mountains.lineTo(0.7 * dScale, 0.61 * dScale);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(244, 249, 252));
        painter.drawPath(mountains);
        painter.drawEllipse(QPointF(0.34 * dScale, 0.34 * dScale),
            0.045 * dScale, 0.045 * dScale);

        QPen magnifierPen(QColor(255, 211, 77));
        magnifierPen.setWidthF(std::max<qreal>(1.3, 0.065 * dScale));
        magnifierPen.setCapStyle(Qt::RoundCap);
        painter.setPen(magnifierPen);
        painter.setBrush(QColor(21, 53, 86, 210));
        painter.drawEllipse(QPointF(0.67 * dScale, 0.67 * dScale),
            0.17 * dScale, 0.17 * dScale);
        painter.drawLine(QPointF(0.79 * dScale, 0.79 * dScale),
            QPointF(0.9 * dScale, 0.9 * dScale));
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace ui
