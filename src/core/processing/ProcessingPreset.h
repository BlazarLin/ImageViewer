// 2026-09-06
// 功能：序列化、校验并保存图像预处理参数预设。
// 目的：让常用调图参数可复用，同时隔离异常 JSON 对运行态参数的影响。
#pragma once

#include "ImageProcessor.h"

#include <QJsonObject>
#include <QString>

namespace core::processing {

class ProcessingPreset {
public:
    static QJsonObject toJson(const ProcessingParameters& parameters);
    static bool fromJson(const QJsonObject& object, ProcessingParameters* parameters,
        QString* error);
    static bool save(const QString& path, const ProcessingParameters& parameters,
        QString* error);
    static bool load(const QString& path, ProcessingParameters* parameters,
        QString* error);
};

} // namespace core::processing
