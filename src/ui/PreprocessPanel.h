// 2026-09-06
// 功能：提供可显隐的图像预处理参数界面。
// 目的：在偶发使用时快速预览工业图像处理效果。
#pragma once

#include "core/processing/ImageProcessor.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

namespace ui {

class PreprocessPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreprocessPanel(QWidget* parent = nullptr);

    core::processing::ProcessingParameters parameters() const;
    void setParameters(const core::processing::ProcessingParameters& parameters);
    void setProcessingEnabled(bool bEnabled);

signals:
    void parametersChanged(const core::processing::ProcessingParameters& parameters);
    void resetRequested();

private slots:
    void emitParametersChanged();
    void resetControls();

private:
    QCheckBox* enabledCheck_ = nullptr;
    QSlider* brightnessSlider_ = nullptr;
    QSlider* contrastSlider_ = nullptr;
    QSlider* gammaSlider_ = nullptr;
    QComboBox* channelCombo_ = nullptr;
    QComboBox* smoothCombo_ = nullptr;
    QSpinBox* smoothKernelSpin_ = nullptr;
    QSlider* sharpenSlider_ = nullptr;
    QComboBox* thresholdCombo_ = nullptr;
    QSpinBox* thresholdSpin_ = nullptr;
    QComboBox* edgeCombo_ = nullptr;
    QSpinBox* cannyLowSpin_ = nullptr;
    QSpinBox* cannyHighSpin_ = nullptr;
    QComboBox* morphologyCombo_ = nullptr;
    QSpinBox* morphologyKernelSpin_ = nullptr;
    QSpinBox* morphologyIterationsSpin_ = nullptr;
    bool bUpdatingControls_ = false;
};

} // namespace ui
