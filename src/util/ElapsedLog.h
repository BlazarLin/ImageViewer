#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QDebug>
#include <QtGlobal>

namespace util {

// RAII 计时日志:作用域结束自动打印耗时(ms)。
// 用法: { util::ElapsedLog _t("openFile"); ... }
class ElapsedLog {
public:
    explicit ElapsedLog(const QString& tag)
        : tag_(tag)
    {
        timer_.start();
    }

    ~ElapsedLog()
    {
        qInfo().noquote() << QStringLiteral("[%1] %2 ms")
                                 .arg(tag_)
                                 .arg(timer_.elapsed());
    }

    ElapsedLog(const ElapsedLog&) = delete;
    ElapsedLog& operator=(const ElapsedLog&) = delete;

private:
    QString tag_;
    QElapsedTimer timer_;
};

} // namespace util