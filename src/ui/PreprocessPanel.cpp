// 2026-09-06
// 功能：构建预处理控件并生成强类型处理参数。
// 目的：集中管理参数范围、默认值和复原行为。
#include "PreprocessPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QWidget* makeSliderRow(QSlider*& slider, QLabel*& valueLabel,
    int nMinimum, int nMaximum, int nValue, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(nMinimum, nMaximum);
    slider->setValue(nValue);
    valueLabel = new QLabel(QString::number(nValue), row);
    valueLabel->setObjectName(QString("ValueBadge"));
    valueLabel->setFixedWidth(64);
    valueLabel->setAlignment(Qt::AlignCenter);
    layout->setSpacing(10);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);
    return row;
}

void configureFormLayout(QFormLayout* layout)
{
    layout->setContentsMargins(14, 16, 14, 14);
    layout->setHorizontalSpacing(14);
    layout->setVerticalSpacing(9);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

} // namespace

namespace ui {

PreprocessPanel::PreprocessPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QString("PreprocessPanel"));
    setMinimumWidth(420);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto* switchCard = new QFrame(this);
    switchCard->setObjectName(QString("ProcessingSwitchCard"));
    auto* switchLayout = new QHBoxLayout(switchCard);
    switchLayout->setContentsMargins(12, 8, 12, 8);
    enabledCheck_ = new QCheckBox(tr("启用实时预处理"), switchCard);
    enabledCheck_->setObjectName(QString("ProcessingSwitch"));
    switchLayout->addWidget(enabledCheck_);
    switchLayout->addStretch(1);
    rootLayout->addWidget(switchCard);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QString("PreprocessScroll"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scrollArea);
    content->setObjectName(QString("PreprocessParameters"));
    parametersContainer_ = content;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(2, 2, 2, 2);
    contentLayout->setSpacing(12);

    auto* toneGroup = new QGroupBox(tr("灰度与色调"), content);
    auto* toneLayout = new QFormLayout(toneGroup);
    configureFormLayout(toneLayout);
    channelCombo_ = new QComboBox(toneGroup);
    channelCombo_->addItems({ tr("原始通道"), tr("灰度"), tr("蓝通道"), tr("绿通道"), tr("红通道") });
    toneLayout->addRow(tr("通道"), channelCombo_);

    QLabel* brightnessValue = nullptr;
    toneLayout->addRow(tr("亮度"), makeSliderRow(
        brightnessSlider_, brightnessValue, -255, 255, 0, toneGroup));
    QLabel* contrastValue = nullptr;
    toneLayout->addRow(tr("对比度 %"), makeSliderRow(
        contrastSlider_, contrastValue, 10, 300, 100, toneGroup));
    QLabel* gammaValue = nullptr;
    toneLayout->addRow(tr("Gamma %"), makeSliderRow(
        gammaSlider_, gammaValue, 10, 500, 100, toneGroup));
    contentLayout->addWidget(toneGroup);

    auto* filterGroup = new QGroupBox(tr("滤波与锐化"), content);
    auto* filterLayout = new QFormLayout(filterGroup);
    configureFormLayout(filterLayout);
    smoothCombo_ = new QComboBox(filterGroup);
    smoothCombo_->addItems({ tr("无"), tr("均值"), tr("高斯"), tr("中值") });
    filterLayout->addRow(tr("平滑"), smoothCombo_);
    smoothKernelSpin_ = new QSpinBox(filterGroup);
    smoothKernelSpin_->setRange(1, 31);
    smoothKernelSpin_->setSingleStep(2);
    smoothKernelSpin_->setValue(3);
    filterLayout->addRow(tr("平滑核"), smoothKernelSpin_);
    QLabel* sharpenValue = nullptr;
    filterLayout->addRow(tr("锐化 %"), makeSliderRow(
        sharpenSlider_, sharpenValue, 0, 300, 0, filterGroup));
    contentLayout->addWidget(filterGroup);

    auto* binaryGroup = new QGroupBox(tr("阈值与边缘"), content);
    auto* binaryLayout = new QFormLayout(binaryGroup);
    configureFormLayout(binaryLayout);
    thresholdCombo_ = new QComboBox(binaryGroup);
    thresholdCombo_->addItems({ tr("无"), tr("固定阈值"), tr("Otsu 自动阈值") });
    binaryLayout->addRow(tr("阈值化"), thresholdCombo_);
    thresholdSpin_ = new QSpinBox(binaryGroup);
    thresholdSpin_->setRange(0, 255);
    thresholdSpin_->setValue(128);
    binaryLayout->addRow(tr("阈值"), thresholdSpin_);
    edgeCombo_ = new QComboBox(binaryGroup);
    edgeCombo_->addItems({ tr("无"), tr("Sobel"), tr("Laplacian"), tr("Canny") });
    binaryLayout->addRow(tr("边缘"), edgeCombo_);
    cannyLowSpin_ = new QSpinBox(binaryGroup);
    cannyLowSpin_->setRange(0, 255);
    cannyLowSpin_->setValue(80);
    binaryLayout->addRow(tr("Canny 低阈值"), cannyLowSpin_);
    cannyHighSpin_ = new QSpinBox(binaryGroup);
    cannyHighSpin_->setRange(0, 255);
    cannyHighSpin_->setValue(160);
    binaryLayout->addRow(tr("Canny 高阈值"), cannyHighSpin_);
    contentLayout->addWidget(binaryGroup);

    auto* morphGroup = new QGroupBox(tr("形态学"), content);
    auto* morphLayout = new QFormLayout(morphGroup);
    configureFormLayout(morphLayout);
    morphologyCombo_ = new QComboBox(morphGroup);
    morphologyCombo_->addItems({ tr("无"), tr("腐蚀"), tr("膨胀"), tr("开运算"), tr("闭运算") });
    morphLayout->addRow(tr("算子"), morphologyCombo_);
    morphologyKernelSpin_ = new QSpinBox(morphGroup);
    morphologyKernelSpin_->setRange(1, 21);
    morphologyKernelSpin_->setSingleStep(2);
    morphologyKernelSpin_->setValue(3);
    morphLayout->addRow(tr("结构元"), morphologyKernelSpin_);
    morphologyIterationsSpin_ = new QSpinBox(morphGroup);
    morphologyIterationsSpin_->setRange(1, 10);
    morphologyIterationsSpin_->setValue(1);
    morphLayout->addRow(tr("迭代次数"), morphologyIterationsSpin_);
    contentLayout->addWidget(morphGroup);
    contentLayout->addStretch(1);

    scrollArea->setWidget(content);
    rootLayout->addWidget(scrollArea, 1);
    auto* resetButton = new QPushButton(tr("复原全部参数"), this);
    resetButton->setObjectName(QString("SecondaryButton"));
    resetButton->setMinimumHeight(34);
    rootLayout->addWidget(resetButton);

    const QList<QComboBox*> sizedCombos = {
        channelCombo_, smoothCombo_, thresholdCombo_, edgeCombo_, morphologyCombo_
    };
    for (QComboBox* combo : sizedCombos) {
        combo->setMinimumHeight(30);
    }
    const QList<QSpinBox*> sizedSpins = { smoothKernelSpin_, thresholdSpin_, cannyLowSpin_,
        cannyHighSpin_, morphologyKernelSpin_, morphologyIterationsSpin_ };
    for (QSpinBox* spin : sizedSpins) {
        spin->setMinimumHeight(30);
    }

    const QList<QSlider*> sliders = { brightnessSlider_, contrastSlider_, gammaSlider_, sharpenSlider_ };
    for (QSlider* slider : sliders) {
        connect(slider, &QSlider::valueChanged, this, &PreprocessPanel::emitParametersChanged);
    }
    connect(brightnessSlider_, &QSlider::valueChanged, brightnessValue,
        [brightnessValue](int nValue) { brightnessValue->setText(QString::number(nValue)); });
    connect(contrastSlider_, &QSlider::valueChanged, contrastValue,
        [contrastValue](int nValue) { contrastValue->setText(QString::number(nValue)); });
    connect(gammaSlider_, &QSlider::valueChanged, gammaValue,
        [gammaValue](int nValue) { gammaValue->setText(QString::number(nValue)); });
    connect(sharpenSlider_, &QSlider::valueChanged, sharpenValue,
        [sharpenValue](int nValue) { sharpenValue->setText(QString::number(nValue)); });

    const QList<QComboBox*> combos = { channelCombo_, smoothCombo_, thresholdCombo_, edgeCombo_, morphologyCombo_ };
    for (QComboBox* combo : combos) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateControlStates();
                emitParametersChanged();
            });
    }
    const QList<QSpinBox*> spins = { smoothKernelSpin_, thresholdSpin_, cannyLowSpin_, cannyHighSpin_,
        morphologyKernelSpin_, morphologyIterationsSpin_ };
    for (QSpinBox* spin : spins) {
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PreprocessPanel::emitParametersChanged);
    }
    connect(enabledCheck_, &QCheckBox::toggled, this, [this](bool) {
        updateControlStates();
        emitParametersChanged();
    });
    connect(resetButton, &QPushButton::clicked, this, &PreprocessPanel::resetControls);
    updateControlStates();
}

core::processing::ProcessingParameters PreprocessPanel::parameters() const
{
    core::processing::ProcessingParameters parameters;
    parameters.bEnabled = enabledCheck_->isChecked();
    parameters.nBrightness = brightnessSlider_->value();
    parameters.dContrast = contrastSlider_->value() / 100.0;
    parameters.dGamma = gammaSlider_->value() / 100.0;
    parameters.channel = static_cast<core::processing::ChannelView>(channelCombo_->currentIndex());
    parameters.smooth = static_cast<core::processing::SmoothMode>(smoothCombo_->currentIndex());
    parameters.nSmoothKernel = smoothKernelSpin_->value();
    parameters.dSharpenAmount = sharpenSlider_->value() / 100.0;
    parameters.threshold = static_cast<core::processing::ThresholdMode>(thresholdCombo_->currentIndex());
    parameters.nThreshold = thresholdSpin_->value();
    parameters.edge = static_cast<core::processing::EdgeMode>(edgeCombo_->currentIndex());
    parameters.nCannyLow = cannyLowSpin_->value();
    parameters.nCannyHigh = cannyHighSpin_->value();
    parameters.morphology = static_cast<core::processing::MorphologyMode>(morphologyCombo_->currentIndex());
    parameters.nMorphKernel = morphologyKernelSpin_->value();
    parameters.nMorphIterations = morphologyIterationsSpin_->value();
    return parameters;
}

void PreprocessPanel::setProcessingEnabled(bool bEnabled)
{
    enabledCheck_->setChecked(bEnabled);
}

void PreprocessPanel::setParameters(const core::processing::ProcessingParameters& parameters)
{
    bUpdatingControls_ = true;
    enabledCheck_->setChecked(parameters.bEnabled);
    brightnessSlider_->setValue(parameters.nBrightness);
    contrastSlider_->setValue(qRound(parameters.dContrast * 100.0));
    gammaSlider_->setValue(qRound(parameters.dGamma * 100.0));
    channelCombo_->setCurrentIndex(static_cast<int>(parameters.channel));
    smoothCombo_->setCurrentIndex(static_cast<int>(parameters.smooth));
    smoothKernelSpin_->setValue(parameters.nSmoothKernel);
    sharpenSlider_->setValue(qRound(parameters.dSharpenAmount * 100.0));
    thresholdCombo_->setCurrentIndex(static_cast<int>(parameters.threshold));
    thresholdSpin_->setValue(parameters.nThreshold);
    edgeCombo_->setCurrentIndex(static_cast<int>(parameters.edge));
    cannyLowSpin_->setValue(parameters.nCannyLow);
    cannyHighSpin_->setValue(parameters.nCannyHigh);
    morphologyCombo_->setCurrentIndex(static_cast<int>(parameters.morphology));
    morphologyKernelSpin_->setValue(parameters.nMorphKernel);
    morphologyIterationsSpin_->setValue(parameters.nMorphIterations);
    bUpdatingControls_ = false;
    updateControlStates();
    emit parametersChanged(this->parameters());
}

void PreprocessPanel::emitParametersChanged()
{
    if (!bUpdatingControls_) {
        emit parametersChanged(parameters());
    }
}

void PreprocessPanel::resetControls()
{
    bUpdatingControls_ = true;
    enabledCheck_->setChecked(false);
    brightnessSlider_->setValue(0);
    contrastSlider_->setValue(100);
    gammaSlider_->setValue(100);
    channelCombo_->setCurrentIndex(0);
    smoothCombo_->setCurrentIndex(0);
    smoothKernelSpin_->setValue(3);
    sharpenSlider_->setValue(0);
    thresholdCombo_->setCurrentIndex(0);
    thresholdSpin_->setValue(128);
    edgeCombo_->setCurrentIndex(0);
    cannyLowSpin_->setValue(80);
    cannyHighSpin_->setValue(160);
    morphologyCombo_->setCurrentIndex(0);
    morphologyKernelSpin_->setValue(3);
    morphologyIterationsSpin_->setValue(1);
    bUpdatingControls_ = false;
    updateControlStates();
    emit resetRequested();
    emit parametersChanged(parameters());
}

void PreprocessPanel::updateControlStates()
{
    const bool bEnabled = enabledCheck_->isChecked();
    if (parametersContainer_) {
        parametersContainer_->setEnabled(bEnabled);
    }
    smoothKernelSpin_->setEnabled(bEnabled && smoothCombo_->currentIndex() != 0);
    thresholdSpin_->setEnabled(bEnabled && thresholdCombo_->currentIndex() == 1);
    const bool bCanny = bEnabled && edgeCombo_->currentIndex() == 3;
    cannyLowSpin_->setEnabled(bCanny);
    cannyHighSpin_->setEnabled(bCanny);
    const bool bMorphology = bEnabled && morphologyCombo_->currentIndex() != 0;
    morphologyKernelSpin_->setEnabled(bMorphology);
    morphologyIterationsSpin_->setEnabled(bMorphology);
}

} // namespace ui
