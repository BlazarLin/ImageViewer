// 2026-09-06
// 功能：实现版本化预处理预设 JSON 的保存、载入和范围归一化。
// 目的：提供稳定、原子写入且向后可诊断的参数交换格式。
#include "ProcessingPreset.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>

namespace {

int oddClamped(int nValue, int nMinimum, int nMaximum)
{
    int nResult = std::clamp(nValue, nMinimum, nMaximum);
    if ((nResult & 1) == 0) {
        nResult = std::min(nResult + 1, nMaximum);
    }
    return nResult;
}

template <typename T>
T enumValue(const QJsonObject& object, const QString& key, int nMaximum, T fallback)
{
    const int nValue = object.value(key).toInt(static_cast<int>(fallback));
    return static_cast<T>(std::clamp(nValue, 0, nMaximum));
}

} // namespace

namespace core::processing {

QJsonObject ProcessingPreset::toJson(const ProcessingParameters& parameters)
{
    QJsonObject values;
    values.insert(QString("enabled"), parameters.bEnabled);
    values.insert(QString("brightness"), parameters.nBrightness);
    values.insert(QString("contrast"), parameters.dContrast);
    values.insert(QString("gamma"), parameters.dGamma);
    values.insert(QString("channel"), static_cast<int>(parameters.channel));
    values.insert(QString("smooth"), static_cast<int>(parameters.smooth));
    values.insert(QString("smoothKernel"), parameters.nSmoothKernel);
    values.insert(QString("sharpenAmount"), parameters.dSharpenAmount);
    values.insert(QString("threshold"), static_cast<int>(parameters.threshold));
    values.insert(QString("thresholdValue"), parameters.nThreshold);
    values.insert(QString("edge"), static_cast<int>(parameters.edge));
    values.insert(QString("cannyLow"), parameters.nCannyLow);
    values.insert(QString("cannyHigh"), parameters.nCannyHigh);
    values.insert(QString("morphology"), static_cast<int>(parameters.morphology));
    values.insert(QString("morphKernel"), parameters.nMorphKernel);
    values.insert(QString("morphIterations"), parameters.nMorphIterations);

    QJsonObject root;
    root.insert(QString("format"), QString("ImageViewerProcessingPreset"));
    root.insert(QString("version"), 1);
    root.insert(QString("parameters"), values);
    return root;
}

bool ProcessingPreset::fromJson(const QJsonObject& object,
    ProcessingParameters* parameters, QString* error)
{
    if (!parameters) {
        if (error) { *error = QString("输出参数为空"); }
        return false;
    }
    if (object.value(QString("format")).toString() != QString("ImageViewerProcessingPreset")
        || object.value(QString("version")).toInt(-1) != 1
        || !object.value(QString("parameters")).isObject()) {
        if (error) { *error = QString("不是受支持的 ImageViewer 预处理预设"); }
        return false;
    }

    const QJsonObject values = object.value(QString("parameters")).toObject();
    ProcessingParameters parsed;
    parsed.bEnabled = values.value(QString("enabled")).toBool(false);
    parsed.nBrightness = std::clamp(values.value(QString("brightness")).toInt(0), -255, 255);
    parsed.dContrast = std::clamp(values.value(QString("contrast")).toDouble(1.0), 0.1, 3.0);
    parsed.dGamma = std::clamp(values.value(QString("gamma")).toDouble(1.0), 0.1, 5.0);
    parsed.channel = enumValue(values, QString("channel"), 4, ChannelView::Original);
    parsed.smooth = enumValue(values, QString("smooth"), 3, SmoothMode::None);
    parsed.nSmoothKernel = oddClamped(values.value(QString("smoothKernel")).toInt(3), 1, 31);
    parsed.dSharpenAmount = std::clamp(values.value(QString("sharpenAmount")).toDouble(0.0), 0.0, 3.0);
    parsed.threshold = enumValue(values, QString("threshold"), 2, ThresholdMode::None);
    parsed.nThreshold = std::clamp(values.value(QString("thresholdValue")).toInt(128), 0, 255);
    parsed.edge = enumValue(values, QString("edge"), 3, EdgeMode::None);
    parsed.nCannyLow = std::clamp(values.value(QString("cannyLow")).toInt(80), 0, 255);
    parsed.nCannyHigh = std::clamp(values.value(QString("cannyHigh")).toInt(160), 0, 255);
    if (parsed.nCannyHigh < parsed.nCannyLow) { std::swap(parsed.nCannyLow, parsed.nCannyHigh); }
    parsed.morphology = enumValue(values, QString("morphology"), 4, MorphologyMode::None);
    parsed.nMorphKernel = oddClamped(values.value(QString("morphKernel")).toInt(3), 1, 21);
    parsed.nMorphIterations = std::clamp(values.value(QString("morphIterations")).toInt(1), 1, 10);
    *parameters = parsed;
    if (error) { error->clear(); }
    return true;
}

bool ProcessingPreset::save(const QString& path,
    const ProcessingParameters& parameters, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) { *error = file.errorString(); }
        return false;
    }
    if (file.write(QJsonDocument(toJson(parameters)).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (error) { *error = file.errorString(); }
        return false;
    }
    if (error) { error->clear(); }
    return true;
}

bool ProcessingPreset::load(const QString& path, ProcessingParameters* parameters,
    QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) { *error = file.errorString(); }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) { *error = QString("JSON 解析失败：%1").arg(parseError.errorString()); }
        return false;
    }
    return fromJson(document.object(), parameters, error);
}

} // namespace core::processing
