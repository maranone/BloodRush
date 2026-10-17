// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Vulkan {
// These occupancy categories describe sampled depth, never a bulb's intensity.
constexpr const char* RobDepthContentState(float far_pct) {
    return far_pct >= 98.f ? "EMPTY" : (far_pct < 90.f ? "POPULATED" : "SPARSE");
}
} // namespace Vulkan
