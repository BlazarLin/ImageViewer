// 2026-09-06
// 功能：显示鼠标像素值、ROI 通道统计与灰度直方图。
// 目的：提供默认隐藏、按需启用的工业图像量测界面。
#pragma once

#include "core/analysis/ImageAnalysis.h"

#include <QWidget>

class QLabel;

namespace ui {

class HistogramWidget : public QWidget {
public:
    explicit HistogramWidget(QWidget* parent = nullptr);
    void setHistogram(const QVector<quint64>& histogram);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<quint64> histogram_;
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
    HistogramWidget* histogram_ = nullptr;
};

} // namespace ui

