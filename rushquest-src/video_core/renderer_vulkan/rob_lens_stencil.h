// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace Vulkan {
struct RobStencilFace {
    bool less_equal;
    std::uint32_t reference;
    std::uint32_t compare_mask;
    bool keep_fail;
    bool keep_pass;
    bool keep_depth_fail;
};

// Captured ROB lens draws use a full-byte, read-only comparison against 16.
// A front-face match must never bypass a different or writable back-face test.
constexpr bool IsRobLensStencilBypass(const RobStencilFace& front,
                                      const RobStencilFace& back, bool two_sided) {
    const auto lens = [](const RobStencilFace& face) {
        return face.less_equal && face.reference == 16 && face.compare_mask == 255 &&
               face.keep_fail && face.keep_pass && face.keep_depth_fail;
    };
    return lens(front) && (!two_sided || lens(back));
}
} // namespace Vulkan
