// 2026-09-24
// 功能：按职责隔离后台线程池，提供协作取消和常数成本图像类型判断。
// 目的：限制并发资源，保证当前图像加载不排在缩略图、统计任务之后。
#pragma once

#include <QImage>
#include <QThreadPool>
#include <atomic>
#include <memory>

namespace util {
using Cancellation = std::shared_ptr<std::atomic_bool>;
inline bool canceled(const Cancellation& flag)
{
    return flag && flag->load(std::memory_order_relaxed);
}
inline void cancel(const Cancellation& flag)
{
    if (flag) { flag->store(true, std::memory_order_relaxed); }
}
inline bool grayFormat(const QImage& image)
{
    return image.format() == QImage::Format_Grayscale8
        || image.format() == QImage::Format_Grayscale16
        || (image.depth() <= 8 && image.isGrayscale());
}
inline bool highDepth(const QImage& image)
{
    return image.format() == QImage::Format_Grayscale16 || image.depth() > 32;
}
enum class WorkPool { Decode, Prefetch, Compute, Thumbnail, Directory, Save };
inline QThreadPool* workPool(WorkPool kind)
{
    struct Pools {
        QThreadPool pools[6];
        Pools()
        {
            for (auto& pool : pools) { pool.setMaxThreadCount(1); }
            pools[0].setMaxThreadCount(2);
            pools[2].setMaxThreadCount(2);
        }
    };
    static Pools instance;
    return &instance.pools[static_cast<int>(kind)];
}
inline void waitForBackgroundWork()
{
    for (auto kind : { WorkPool::Decode, WorkPool::Prefetch, WorkPool::Compute,
        WorkPool::Thumbnail, WorkPool::Directory, WorkPool::Save }) {
        workPool(kind)->waitForDone();
    }
}
} // namespace util
