// 2026-09-06
// 功能：显示鼠标像素值、ROI 通道统计与灰度直方图。
// 目的：提供默认隐藏、按需启用的工业图像量测界面。
#pragma once

#include "core/analysis/ImageAnalysis.h"

#include <QWidget>

class QLabel;
class QGroupBox;

namespace ui {

class HistogramWidget : public QWidget {
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    void setHistogram(const QVector<quint64>& histogram, const QColor& color = QColor(180, 190, 200));

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QVector<quint64> histogram_;
    QColor color_;
};

class AnalysisPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisPanel(QWidget* parent = nullptr);

    void setPixel(const QPoint& position, const QColor& color, bool bValid);
    void setSelection(const QRect& region);
    void setStatistics(const core::analysis::AnalysisResult& result);
    void setBusy(bool bBusy);
    void clear();

private:
    QLabel* coordinateValue_ = nullptr;
    QLabel* rgbValue_ = nullptr;
    QLabel* grayValue_ = nullptr;
    QLabel* hsvValue_ = nullptr;
    QLabel* labValue_ = nullptr;
    QLabel* regionValue_ = nullptr;
    QLabel* countValue_ = nullptr;
    QLabel* redStatistics_ = nullptr;
    QLabel* greenStatistics_ = nullptr;
    QLabel* blueStatistics_ = nullptr;
    QLabel* stateValue_ = nullptr;
    QGroupBox* histogramGroup_ = nullptr;
    std::array<HistogramWidget*, 3> histograms_ = { nullptr, nullptr, nullptr };
    std::array<QLabel*, 3> histogramLabels_ = { nullptr, nullptr, nullptr };
};

} // namespace ui

