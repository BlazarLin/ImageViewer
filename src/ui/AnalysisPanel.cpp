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
#include <QTabWidget>
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
    setMinimumHeight(150);
    setMouseTracking(true);
}

void HistogramWidget::setHistogram(const QVector<quint64>& histogram, const QColor& color)
{
    histogram_ = histogram;
    color_ = color;
    QToolTip::hideText();
    update();
}

void HistogramWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 33, 36));
    if (histogram_.size() != 256) {
        return;
    }
    const quint64 nMaximum = *std::max_element(histogram_.begin(), histogram_.end());
    const int nLeft = std::max(48, fontMetrics().horizontalAdvance(QString::number(nMaximum)) + 8);
    const QRect plot(nLeft, 10, width() - nLeft - 12, height() - 40);
    if (nMaximum == 0 || plot.width() < 2 || plot.height() < 2) { return; }
    painter.setPen(QColor(72, 75, 80));
    painter.drawRect(plot);
    painter.setPen(QColor(185, 193, 202));
    painter.drawText(QRect(0, plot.top(), nLeft - 6, fontMetrics().height()),
        Qt::AlignRight | Qt::AlignTop, QString::number(nMaximum));
    painter.drawText(QRect(0, plot.bottom() - fontMetrics().height(), nLeft - 6, fontMetrics().height()),
        Qt::AlignRight | Qt::AlignBottom, QString("0"));
    painter.drawText(QRect(plot.left(), plot.bottom() + 3, plot.width(), 30),
        Qt::AlignLeft | Qt::AlignTop, QString("0"));
    painter.drawText(QRect(plot.left(), plot.bottom() + 3, plot.width(), 30),
        Qt::AlignRight | Qt::AlignTop, QString("255"));
    painter.setPen(QPen(color_, std::max(1.0, plot.width() / 256.0)));
    for (int nBin = 0; nBin < 256; ++nBin) {
        const double dRatio = static_cast<double>(histogram_[nBin]) / nMaximum;
        const int nX = plot.left() + qRound(nBin * (plot.width() - 1) / 255.0);
        const int nHeight = qRound(dRatio * (plot.height() - 1));
        painter.drawLine(nX, plot.bottom(), nX, plot.bottom() - nHeight);
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (histogram_.size() != 256) { return; }
    const quint64 nMaximum = *std::max_element(histogram_.begin(), histogram_.end());
    const int nLeft = std::max(48, fontMetrics().horizontalAdvance(QString::number(nMaximum)) + 8);
    const int nPlotWidth = width() - nLeft - 12;
    if (nPlotWidth < 2 || event->x() < nLeft || event->x() >= nLeft + nPlotWidth) {
        QToolTip::hideText();
        return;
    }
    const int nBin = std::clamp(qRound((event->x() - nLeft) * 255.0 / (nPlotWidth - 1)), 0, 255);
    QToolTip::showText(event->globalPos(), tr("灰度档 %1：%2 像素").arg(nBin).arg(histogram_[nBin]), this);
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
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QString("AnalysisTabs"));
    outerLayout->addWidget(tabs);
    auto* scroll = new QScrollArea(tabs);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setObjectName(QString("AnalysisContent"));
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    content->setObjectName(QString("AnalysisContent"));
    scroll->setWidget(content);
    tabs->addTab(scroll, tr("像素与统计"));
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

    auto* histogramScroll = new QScrollArea(tabs);
    histogramScroll->setFrameShape(QFrame::NoFrame);
    histogramScroll->viewport()->setObjectName(QString("AnalysisContent"));
    histogramScroll->setWidgetResizable(true);
    auto* histogramContent = new QWidget(histogramScroll);
    histogramContent->setObjectName(QString("AnalysisContent"));
    histogramScroll->setWidget(histogramContent);
    auto* histogramRoot = new QVBoxLayout(histogramContent);
    histogramRoot->setContentsMargins(8, 8, 8, 8);
    histogramGroup_ = new QGroupBox(tr("直方图"), histogramContent);
    histogramRoot->addWidget(histogramGroup_);
    tabs->addTab(histogramScroll, tr("直方图"));
    histogramGroup_->setObjectName(QString("HistogramGroup"));
    auto* histogramLayout = new QVBoxLayout(histogramGroup_);
    histogramLayout->setContentsMargins(12, 16, 12, 12);
    auto* histogramHint = new QLabel(tr("横轴：0–255；纵轴：像素数（各通道独立刻度）\n悬停查看每档计数"), histogramGroup_);
    histogramHint->setWordWrap(true);
    histogramLayout->addWidget(histogramHint);
    for (int nChannel = 0; nChannel < 3; ++nChannel) {
        histogramLabels_[nChannel] = new QLabel(histogramGroup_);
        histograms_[nChannel] = new HistogramWidget(histogramGroup_);
        histograms_[nChannel]->setObjectName(QString("Histogram%1").arg(nChannel));
        histogramLayout->addWidget(histogramLabels_[nChannel]);
        histogramLayout->addWidget(histograms_[nChannel]);
    }
    histogramLayout->addStretch(1);
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
    hsvValue_->setText(QString("%1°, %2, %3").arg(nHue).arg(nSaturation).arg(nValue));
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
    const std::array<QColor, 3> colors = { QColor(255, 100, 100), QColor(90, 215, 130), QColor(100, 165, 255) };
    const std::array<QString, 3> names = { tr("R · 红通道"), tr("G · 绿通道"), tr("B · 蓝通道") };
    for (int nChannel = 0; nChannel < 3; ++nChannel) {
        const bool bVisible = result.bColor || nChannel == 0;
        histogramLabels_[nChannel]->setVisible(bVisible);
        histograms_[nChannel]->setVisible(bVisible);
        histogramLabels_[nChannel]->setText(result.bColor ? names[nChannel] : tr("灰度"));
        histograms_[nChannel]->setHistogram(result.bColor ? result.rgbHistograms[nChannel] : result.grayHistogram,
            result.bColor ? colors[nChannel] : QColor(180, 190, 200));
    }
    stateValue_->setText(tr("完成"));
}

void AnalysisPanel::setBusy(bool bBusy)
{
    if (bBusy) {
        redStatistics_->setText(QString("-"));
        greenStatistics_->setText(QString("-"));
        blueStatistics_->setText(QString("-"));
        countValue_->setText(QString("-"));
        for (auto* histogram : histograms_) { histogram->setHistogram({}); }
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
    for (int nChannel = 0; nChannel < 3; ++nChannel) {
        histogramLabels_[nChannel]->clear();
        histogramLabels_[nChannel]->hide();
        histograms_[nChannel]->setHistogram({});
        histograms_[nChannel]->setVisible(nChannel == 0);
    }
}

} // namespace ui
