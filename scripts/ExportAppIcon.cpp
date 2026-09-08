// 2026-09-08
// 功能：将应用内绘制的图标导出为多尺寸 Windows ICO。
// 目的：让 EXE、资源管理器与标题栏使用相同图标，并可重复生成资源。
#include "ui/AppIcon.h"

#include <QBuffer>
#include <QDataStream>
#include <QGuiApplication>
#include <QSaveFile>
#include <QVector>

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    if (app.arguments().size() != 2) {
        return 1;
    }
    const QIcon icon = ui::createAppIcon();
    const QVector<int> sizes = { 16, 24, 32, 48, 64, 128, 256 };
    QVector<QByteArray> frames;
    for (int nSize : sizes) {
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly)
            || !icon.pixmap(nSize, nSize).save(&buffer, "PNG")) {
            return 2;
        }
        frames.append(png);
    }
    QSaveFile file(app.arguments().at(1));
    if (!file.open(QIODevice::WriteOnly)) {
        return 3;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 nOffset = 6 + 16 * static_cast<quint32>(sizes.size());
    for (int nIndex = 0; nIndex < sizes.size(); ++nIndex) {
        const quint8 nDimension = static_cast<quint8>(sizes.at(nIndex) % 256);
        const quint32 nBytes = static_cast<quint32>(frames.at(nIndex).size());
        stream << nDimension << nDimension << quint8(0) << quint8(0)
               << quint16(1) << quint16(32) << nBytes << nOffset;
        nOffset += nBytes;
    }
    for (const QByteArray& frame : frames) {
        if (stream.writeRawData(frame.constData(), frame.size()) != frame.size()) {
            return 4;
        }
    }
    return stream.status() == QDataStream::Ok && file.commit() ? 0 : 5;
}
