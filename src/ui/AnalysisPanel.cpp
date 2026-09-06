// 2026-09-06
// 功能：实现像素信息、ROI 统计和灰度直方图界面。
// 目的：让偶发分析操作不占用常规看图空间。
#include "AnalysisPanel.h"

#include <QColor>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
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
    setMinimumHeight(130);
}

void HistogramWidget::setHistogram(const QVector<quint64>& histogram)
{
    histogram_ = histogram;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 33, 36));
    painter.setPen(QColor(72, 75, 80));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    if (histogram_.size() != 256) {
        return;
    }
    quint64 nMaximum = 0;
    for (quint64 nValue : histogram_) {
        nMaximum = std::max(nMaximum, nValue);
    }
    if (nMaximum == 0) {
        return;
    }
    painter.setPen(QColor(104, 180, 225));
    const double dWidth = static_cast<double>(width() - 2) / 256.0;
    const int nBottom = height() - 2;
    for (int nBin = 0; nBin < 256; ++nBin) {
        const double dRatio = static_cast<double>(histogram_[nBin]) / nMaximum;
        const int nX = 1 + static_cast<int>(std::floor(nBin * dWidth));
        const int nHeight = static_cast<int>(std::round(dRatio * (height() - 4)));
        painter.drawLine(nX, nBottom, nX, nBottom - nHeight);
    }
}

AnalysisPanel::AnalysisPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QString("AnalysisPanel"));
    setMinimumWidth(340);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);
    auto* pixelGroup = new QGroupBox(tr("光标像素"), this);
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

    auto* roiGroup = new QGroupBox(tr("ROI 统计"), this);
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

    auto* histogramGroup = new QGroupBox(tr("灰度直方图"), this);
    auto* histogramLayout = new QVBoxLayout(histogramGroup);
    histogramLayout->setContentsMargins(12, 16, 12, 12);
    histogram_ = new HistogramWidget(histogramGroup);
    histogramLayout->addWidget(histogram_);
    rootLayout->addWidget(histogramGroup);
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
    histogram_->setHistogram(result.grayHistogram);
    stateValue_->setText(tr("完成"));
}

void AnalysisPanel::setBusy(bool bBusy)
{
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
    histogram_->setHistogram(QVector<quint64>());
}

} // namespace ui
