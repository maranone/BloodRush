// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Vulkan {
// Pixel centers covered by a rectangular clear, intersected with its scissor and target.
// Negative viewport height is normal for the guest's inverted Y coordinates.
inline std::array<std::uint32_t, 4> RobDepthClearRegion(
    double x, double y, double width, double height,
    std::int32_t scissor_x, std::int32_t scissor_y,
    std::uint32_t scissor_width, std::uint32_t scissor_height,
    std::uint32_t target_width, std::uint32_t target_height) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)) {
        return {0, 0, 0, 0};
    }
    const double x0 = std::min(x, x + width), x1 = std::max(x, x + width);
    const double y0 = std::min(y, y + height), y1 = std::max(y, y + height);
    const auto bound = [](double position, std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::ceil(std::clamp(position - .5, 0., double(limit))));
    };
    const auto left = bound(std::max(x0, double(scissor_x)), target_width);
    const auto top = bound(std::max(y0, double(scissor_y)), target_height);
    const auto right = bound(std::min(x1, double(scissor_x) + scissor_width), target_width);
    const auto bottom = bound(std::min(y1, double(scissor_y) + scissor_height), target_height);
    return {left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0};
}
} // namespace Vulkan
