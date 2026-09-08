// 2026-09-06
// 功能：实现像素信息、ROI 统计和灰度直方图界面。
// 目的：让偶发分析操作不占用常规看图空间。
#include "AnalysisPanel.h"

#include <QColor>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QMouseEvent>
#include <QToolTip>
#include <QPushButton>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString formatStatistics(const core::analysis::ChannelStatistics& value)
{
    return QString("%1 / %2 / %3 / %4")
        .arg(QString::number(value.dMinimum, 'f', 0))
        .arg(QString::number(value.dMaximum, 'f', 0))
        .arg(QString::number(value.dMean, 'f', 2))
        .arg(QString::number(value.dStandardDeviation, 'f', 2));
}

} // namespace

namespace ui {

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
    setMouseTracking(true);
}

void HistogramWidget::setResult(const core::analysis::AnalysisResult& result)
{
    bColor_ = result.bColor;
    histograms_ = result.rgbHistograms;
    if (!bColor_) { histograms_[0] = result.grayHistogram; }
    nMaximum_ = 0;
    for (int nChannel = 0; nChannel < (bColor_ ? 3 : 1); ++nChannel) {
        for (quint64 nCount : histograms_[nChannel]) { nMaximum_ = std::max(nMaximum_, nCount); }
    }
    QToolTip::hideText();
    update();
}

void HistogramWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 33, 36));
    if (nMaximum_ == 0) { return; }
    const int nLeft = std::max(48, fontMetrics().horizontalAdvance(QString::number(nMaximum_)) + 8);
    const QRect plot(nLeft, 10, width() - nLeft - 12, height() - 40);
    if (plot.width() < 2 || plot.height() < 2) { return; }
    painter.setPen(QColor(72, 75, 80));
    painter.drawRect(plot);
    painter.setPen(QColor(185, 193, 202));
    painter.drawText(QRect(0, plot.top(), nLeft - 6, fontMetrics().height()),
        Qt::AlignRight | Qt::AlignTop, QString::number(nMaximum_));
    painter.drawText(QRect(0, plot.bottom() - fontMetrics().height(), nLeft - 6, fontMetrics().height()),
        Qt::AlignRight | Qt::AlignBottom, QString("0"));
    painter.drawText(QRect(plot.left(), plot.bottom() + 3, plot.width(), 30),
        Qt::AlignLeft | Qt::AlignTop, QString("0"));
    painter.drawText(QRect(plot.left(), plot.bottom() + 3, plot.width(), 30),
        Qt::AlignRight | Qt::AlignTop, QString("255"));
    const std::array<QColor, 3> colors = { QColor(255, 100, 100), QColor(90, 215, 130), QColor(100, 165, 255) };
    painter.setRenderHint(QPainter::Antialiasing);
    for (int nChannel = 0; nChannel < (bColor_ ? 3 : 1); ++nChannel) {
        if (histograms_[nChannel].size() != 256) { continue; }
        QPainterPath curve;
        for (int nBin = 0; nBin < 256; ++nBin) {
            const QPointF point(plot.left() + nBin * (plot.width() - 1) / 255.0,
                plot.bottom() - static_cast<double>(histograms_[nChannel][nBin]) / nMaximum_ * (plot.height() - 1));
            if (nBin == 0) { curve.moveTo(point); } else { curve.lineTo(point); }
        }
        // 共用纵轴保证可比性；线型让完全重合的通道也能辨认。
        QPen pen(bColor_ ? colors[nChannel] : QColor(180, 190, 200), 1.8);
        if (bColor_ && nChannel == 1) { pen.setStyle(Qt::DashLine); }
        if (bColor_ && nChannel == 2) { pen.setStyle(Qt::DotLine); }
        painter.setPen(pen);
        painter.drawPath(curve);
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* event)
{
    const int nLeft = std::max(48, fontMetrics().horizontalAdvance(QString::number(nMaximum_)) + 8);
    const int nPlotWidth = width() - nLeft - 12;
    if (nMaximum_ == 0 || nPlotWidth < 2 || event->x() < nLeft || event->x() >= nLeft + nPlotWidth
        || event->y() < 10 || event->y() > height() - 30) {
        QToolTip::hideText();
        return;
    }
    const int nBin = std::clamp(qRound((event->x() - nLeft) * 255.0 / (nPlotWidth - 1)), 0, 255);
    const QString description = bColor_
        ? tr("灰度档 %1：R %2 / G %3 / B %4 像素").arg(nBin)
            .arg(histograms_[0].value(nBin)).arg(histograms_[1].value(nBin)).arg(histograms_[2].value(nBin))
        : tr("灰度档 %1：%2 像素").arg(nBin).arg(histograms_[0].value(nBin));
    QToolTip::showText(event->globalPos(), description, this);
}

void HistogramWidget::leaveEvent(QEvent* event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

AnalysisPanel::AnalysisPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QString("AnalysisPanel"));
    setMinimumWidth(460);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    outerLayout->addWidget(scroll);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setObjectName(QString("AnalysisContent"));
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    content->setObjectName(QString("AnalysisContent"));
    scroll->setWidget(content);
    auto* rootLayout = new QVBoxLayout(content);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);
    auto* pixelGroup = new QGroupBox(tr("光标像素"), content);
    auto* pixelLayout = new QFormLayout(pixelGroup);
    pixelLayout->setContentsMargins(14, 16, 14, 14);
    pixelLayout->setHorizontalSpacing(14);
    pixelLayout->setVerticalSpacing(7);
    coordinateValue_ = new QLabel(QString("-"), pixelGroup);
    rgbValue_ = new QLabel(QString("-"), pixelGroup);
    grayValue_ = new QLabel(QString("-"), pixelGroup);
    hsvValue_ = new QLabel(QString("-"), pixelGroup);
    labValue_ = new QLabel(QString("-"), pixelGroup);
    pixelLayout->addRow(tr("坐标"), coordinateValue_);
    pixelLayout->addRow(QString("RGB"), rgbValue_);
    pixelLayout->addRow(tr("灰度"), grayValue_);
    pixelLayout->addRow(QString("HSV"), hsvValue_);
    pixelLayout->addRow(QString("Lab"), labValue_);
    rootLayout->addWidget(pixelGroup);

    auto* roiGroup = new QGroupBox(tr("ROI 统计"), content);
    auto* roiLayout = new QFormLayout(roiGroup);
    roiLayout->setContentsMargins(14, 16, 14, 14);
    roiLayout->setHorizontalSpacing(14);
    roiLayout->setVerticalSpacing(7);
    regionValue_ = new QLabel(tr("Shift + 左键拖动选择"), roiGroup);
    countValue_ = new QLabel(QString("-"), roiGroup);
    redStatistics_ = new QLabel(QString("-"), roiGroup);
    greenStatistics_ = new QLabel(QString("-"), roiGroup);
    blueStatistics_ = new QLabel(QString("-"), roiGroup);
    stateValue_ = new QLabel(QString(), roiGroup);
    roiLayout->addRow(tr("范围"), regionValue_);
    roiLayout->addRow(tr("像素数"), countValue_);
    roiLayout->addRow(tr("统计格式"), new QLabel(tr("最小 / 最大 / 均值 / 标准差"), roiGroup));
    roiLayout->addRow(QString("R"), redStatistics_);
    roiLayout->addRow(QString("G"), greenStatistics_);
    roiLayout->addRow(QString("B"), blueStatistics_);
    roiLayout->addRow(tr("状态"), stateValue_);
    rootLayout->addWidget(roiGroup);

    histogramGroup_ = new QGroupBox(tr("直方图"), content);
    histogramGroup_->setObjectName(QString("HistogramGroup"));
    auto* histogramLayout = new QVBoxLayout(histogramGroup_);
    histogramLayout->setContentsMargins(12, 16, 12, 12);
    auto* analyzeButton = new QPushButton(tr("分析整张图"), histogramGroup_);
    analyzeButton->setObjectName(QString("AnalyzeFullImage"));
    connect(analyzeButton, &QPushButton::clicked, this, &AnalysisPanel::analyzeFullImageRequested);
    histogramLayout->addWidget(analyzeButton);
    histogramLegend_ = new QLabel(histogramGroup_);
    histogramLegend_->setWordWrap(true);
    histogramLayout->addWidget(histogramLegend_);
    histogram_ = new HistogramWidget(histogramGroup_);
    histogram_->setObjectName(QString("Histogram"));
    histogramLayout->addWidget(histogram_);
    auto* histogramHint = new QLabel(tr("横轴：0–255；纵轴：像素数（共用刻度）\n悬停查看每档计数；Shift + 拖动分析局部"), histogramGroup_);
    histogramHint->setWordWrap(true);
    histogramLayout->addWidget(histogramHint);
    rootLayout->insertWidget(1, histogramGroup_);
    clear();
    rootLayout->addStretch(1);
}

void AnalysisPanel::setPixel(const QPoint& position, const QColor& color, bool bValid)
{
    if (!bValid) {
        coordinateValue_->setText(QString("-"));
        rgbValue_->setText(QString("-"));
        grayValue_->setText(QString("-"));
        hsvValue_->setText(QString("-"));
        labValue_->setText(QString("-"));
        return;
    }
    coordinateValue_->setText(QString("%1, %2").arg(position.x()).arg(position.y()));
    rgbValue_->setText(QString("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue()));
    const int nGray = (77 * color.red() + 150 * color.green() + 29 * color.blue() + 128) >> 8;
    grayValue_->setText(QString::number(nGray));
    int nHue = 0;
    int nSaturation = 0;
    int nValue = 0;
    color.getHsv(&nHue, &nSaturation, &nValue);
    hsvValue_->setText(QString("%1, %2, %3")
        .arg(nHue < 0 ? tr("无色相") : QString("%1°").arg(nHue)).arg(nSaturation).arg(nValue));
    const double dR = color.redF();
    const double dG = color.greenF();
    const double dB = color.blueF();
    const auto linear = [](double dValue) {
        return dValue > 0.04045 ? std::pow((dValue + 0.055) / 1.055, 2.4) : dValue / 12.92;
    };
    const double dX = (0.4124564 * linear(dR) + 0.3575761 * linear(dG) + 0.1804375 * linear(dB)) / 0.95047;
    const double dY = (0.2126729 * linear(dR) + 0.7151522 * linear(dG) + 0.0721750 * linear(dB));
    const double dZ = (0.0193339 * linear(dR) + 0.1191920 * linear(dG) + 0.9503041 * linear(dB)) / 1.08883;
    const auto labF = [](double dValue) {
        return dValue > 0.008856 ? std::cbrt(dValue) : 7.787 * dValue + 16.0 / 116.0;
    };
    const double dFx = labF(dX);
    const double dFy = labF(dY);
    const double dFz = labF(dZ);
    labValue_->setText(QString("%1, %2, %3")
        .arg(QString::number(116.0 * dFy - 16.0, 'f', 1))
        .arg(QString::number(500.0 * (dFx - dFy), 'f', 1))
        .arg(QString::number(200.0 * (dFy - dFz), 'f', 1)));
}

void AnalysisPanel::setSelection(const QRect& region)
{
    regionValue_->setText(QString("(%1, %2)  %3 × %4")
        .arg(region.x()).arg(region.y()).arg(region.width()).arg(region.height()));
}

void AnalysisPanel::setStatistics(const core::analysis::AnalysisResult& result)
{
    setSelection(result.region);
    countValue_->setText(QString::number(result.nPixelCount));
    redStatistics_->setText(formatStatistics(result.rgb[0]));
    greenStatistics_->setText(formatStatistics(result.rgb[1]));
    blueStatistics_->setText(formatStatistics(result.rgb[2]));
    histogramGroup_->setTitle(result.bColor ? tr("RGB 三通道直方图") : tr("灰度直方图"));
    histogram_->setResult(result);
    histogramLegend_->setText(result.bColor
        ? tr("<span style=\"color:#ff6464\">R 红（实线）</span> · "
             "<span style=\"color:#5ad782\">G 绿（虚线）</span> · "
             "<span style=\"color:#64a5ff\">B 蓝（点线）</span>")
        : tr("灰度"));
    stateValue_->setText(tr("完成"));
}

void AnalysisPanel::setBusy(bool bBusy)
{
    if (bBusy) {
        redStatistics_->setText(QString("-"));
        greenStatistics_->setText(QString("-"));
        blueStatistics_->setText(QString("-"));
        countValue_->setText(QString("-"));
        histogram_->setResult({});
        histogramLegend_->clear();
    }
    stateValue_->setText(bBusy ? tr("计算中…") : QString());
}

void AnalysisPanel::clear()
{
    setPixel(QPoint(), QColor(), false);
    regionValue_->setText(tr("Shift + 左键拖动选择"));
    countValue_->setText(QString("-"));
    redStatistics_->setText(QString("-"));
    greenStatistics_->setText(QString("-"));
    blueStatistics_->setText(QString("-"));
    stateValue_->clear();
    histogramGroup_->setTitle(tr("直方图"));
    histogramLegend_->clear();
    histogram_->setResult({});
}

} // namespace ui
