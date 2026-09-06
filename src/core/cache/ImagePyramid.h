// 2026-09-06
// 功能：为已解码图像构建逐级二分辨率显示金字塔。
// 目的：降低超大图缩小显示时的全分辨率重复采样成本。
#pragma once

#include <QImage>
#include <vector>

namespace core::cache {

class ImagePyramid {
public:
    static std::vector<QImage> build(const QImage& source, int nSmallestSideLimit = 1024,
        int nMaximumLevels = 8);
};

} // namespace core::cache
