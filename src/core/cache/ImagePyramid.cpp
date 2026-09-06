// 2026-09-06
// 功能：实现内存共享起始层和高质量二分降采样金字塔。
// 目的：在后台构建有限层级，避免 UI 线程承担大图缩放工作。
#include "ImagePyramid.h"

#include <algorithm>

namespace core::cache {

std::vector<QImage> ImagePyramid::build(const QImage& source, int nSmallestSideLimit,
    int nMaximumLevels)
{
    std::vector<QImage> levels;
    if (source.isNull() || nMaximumLevels <= 0) {
        return levels;
    }
    levels.reserve(nMaximumLevels);
    levels.push_back(source);
    const int nLimit = std::max(64, nSmallestSideLimit);
    while (static_cast<int>(levels.size()) < nMaximumLevels) {
        const QImage& previous = levels.back();
        if (std::max(previous.width(), previous.height()) <= nLimit) {
            break;
        }
        const QSize nextSize(std::max(1, (previous.width() + 1) / 2),
            std::max(1, (previous.height() + 1) / 2));
        levels.emplace_back(previous.scaled(nextSize, Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation));
    }
    return levels;
}

} // namespace core::cache
