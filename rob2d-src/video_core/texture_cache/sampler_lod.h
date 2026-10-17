// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace VideoCore {
struct SamplerLodClamp { std::uint32_t minimum, maximum; };
// Sampler LOD is relative to the view. The texture descriptor limit is relative
// to the original mip chain. Both hardware fields use unsigned 8.8 fixed point.
constexpr SamplerLodClamp ClampImageMinLod(std::uint32_t sampler_min,
                                         std::uint32_t sampler_max,
                                         std::uint32_t image_min,
                                         std::uint32_t view_base_level) {
    const auto base = view_base_level * 256u;
    const auto relative = image_min > base ? image_min - base : 0u;
    const auto minimum = std::max(sampler_min, relative);
    return {minimum, std::max(sampler_max, minimum)};
}
} // namespace VideoCore
