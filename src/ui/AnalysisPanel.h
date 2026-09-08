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
    void setResult(const core::analysis::AnalysisResult& result);
    void setSmoothingSigma(double dSigma);
    static QVector<double> smoothCounts(const QVector<quint64>& counts, double dSigma);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void rebuildCurves();
    std::array<QVector<quint64>, 3> histograms_;
    std::array<QVector<double>, 3> curves_;
    double dSmoothingSigma_ = 1.0;
    bool bColor_ = false;
    quint64 nMaximum_ = 0;
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

signals:
    void analyzeFullImageRequested();

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
    HistogramWidget* histogram_ = nullptr;
    QLabel* histogramLegend_ = nullptr;
};

} // namespace ui
