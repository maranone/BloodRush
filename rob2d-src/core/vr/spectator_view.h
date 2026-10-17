// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include "core/vr/vr_runtime.h"

namespace Core::Vr {

// Flat: the left eye alone, stretched over the whole window, for playing on a monitor (give the
// title a field of view of the window's shape: SHADPS4_VR_HEADSET_FOV_TAN).
enum class DesktopView { Stereo, Spectator, Combined, Flat };

inline DesktopView ParseDesktopView(std::string_view value) {
    if (value == "combined") {
        return DesktopView::Combined;
    }
    if (value == "flat") {
        return DesktopView::Flat;
    }
    return value == "spectator" ? DesktopView::Spectator : DesktopView::Stereo;
}

inline bool ValidSpectatorFov(const Fov& fov) {
    for (const float tangent : {fov.tan_out, fov.tan_in, fov.tan_top, fov.tan_bottom}) {
        if (!std::isfinite(tangent) || tangent < 0.1f || tangent > 100.0f) {
            return false;
        }
    }
    return true;
}

inline float DesktopViewAspect(DesktopView view, const Fov& fov, float eye_aspect) {
    if (view == DesktopView::Stereo) {
        return eye_aspect * 2.0f;
    }
    if (view == DesktopView::Spectator || !ValidSpectatorFov(fov)) {
        return eye_aspect;
    }
    return eye_aspect * 2.0f * std::max(fov.tan_out, fov.tan_in) / (fov.tan_out + fov.tan_in);
}

struct SpectatorRect {
    s32 x;
    s32 y;
    u32 width;
    u32 height;
};

inline SpectatorRect SpectatorContentRect(u32 width, u32 height, float aspect, bool crop) {
    if (width == 0 || height == 0 || !std::isfinite(aspect) || aspect <= 0.0f) {
        return {0, 0, width, height};
    }
    const bool fill_width = crop || aspect > static_cast<float>(width) / height;
    const u32 content_width = fill_width ? width : std::max(1u, static_cast<u32>(height * aspect));
    const u32 content_height =
        fill_width ? std::max(1u, static_cast<u32>(width / aspect)) : height;
    return {(static_cast<s32>(width) - static_cast<s32>(content_width)) / 2,
            (static_cast<s32>(height) - static_cast<s32>(content_height)) / 2,
            content_width, content_height};
}

/// The part of an eye's picture Flat shows, as {scale_x, scale_y, offset_x, offset_y} of its
/// texture coordinates: the largest rectangle of the window's shape inside the round view a
/// title draws (Rush of Blood: a circle as wide as the smallest of the four angles). Give the
/// title equal angles all round and this keeps the middle, without the circle's edge.
inline std::array<float, 4> FlatEyeCrop(const Fov& fov, float window_aspect) {
    if (!ValidSpectatorFov(fov) || !std::isfinite(window_aspect) || window_aspect <= 0.0f) {
        return {1.0f, 1.0f, 0.0f, 0.0f};
    }
    const float width = fov.tan_out + fov.tan_in;
    const float height = fov.tan_top + fov.tan_bottom;
    const float radius =
        std::min({fov.tan_out, fov.tan_in, fov.tan_top, fov.tan_bottom}) * 0.985f;
    const float diagonal = std::sqrt(1.0f + window_aspect * window_aspect);
    const float half_x = std::min(radius * window_aspect / diagonal, width / 2);
    const float half_y = std::min(radius / diagonal, height / 2);
    return {2 * half_x / width, 2 * half_y / height, (fov.tan_out - half_x) / width,
            (fov.tan_top - half_y) / height};
}

struct HorizontalEyeRegion {
    u32 x;
    u32 width;
    u32 clip_x;
    u32 clip_width;
};

inline std::array<HorizontalEyeRegion, 2> CombinedEyeRegions(u32 width, const Fov& fov) {
    if (!ValidSpectatorFov(fov)) {
        return {{{0, width, 0, width}, {0, width, 0, 0}}};
    }
    const float outer = std::max(fov.tan_out, fov.tan_in);
    const auto start = static_cast<u32>(std::lround(width * (outer - fov.tan_out) / (2 * outer)));
    const auto end = static_cast<u32>(std::lround(width * (outer + fov.tan_in) / (2 * outer)));
    const auto secondary = fov.tan_out >= fov.tan_in
                               ? HorizontalEyeRegion{width - end, end - start, end, width - end}
                               : HorizontalEyeRegion{width - end, end - start, 0, start};
    return {{{start, end - start, start, end - start}, secondary}};
}

}
