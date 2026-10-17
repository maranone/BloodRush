// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <string_view>

namespace Vulkan {

inline float RobLensMaskScale(std::string_view value) {
    float scale = 1.0f;
    if (!value.empty()) {
        const auto result = std::from_chars(value.data(), value.data() + value.size(), scale);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
            !std::isfinite(scale) || scale < 0.0f || scale > 2.0f) {
            return 1.0f;
        }
    }
    return scale;
}

// Centre XY and radius XY in the source texture, not in the destination viewport.
// Measured from CUSA02350's packed-eye captures. The two apertures are slightly
// asymmetric; a single circle at the middle of the packed image masks the wrong area.
inline std::array<float, 4> RobLensMask(std::string_view serial, float source_from,
                                     float source_width, float scale) {
    if (serial != "CUSA02350" || !std::isfinite(scale) || scale <= 0.0f || scale > 2.0f ||
        !std::isfinite(source_from) || !std::isfinite(source_width) ||
        std::abs(source_width - 0.5f) > 0.00001f ||
        (std::abs(source_from) > 0.00001f && std::abs(source_from - 0.5f) > 0.00001f)) {
        return {};
    }
    const float centre_x = source_from < 0.25f ? 0.495f : 0.505f;
    return {source_from + source_width * centre_x, 0.5f,
            source_width * 0.54f * scale, 0.51f * scale};
}

} // namespace Vulkan
