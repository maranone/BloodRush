// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include "core/known_title_builds.h"

namespace Core::KnownTitle::RobBuilds {

// CUSA02350 1.00, SHA256 of the inspected fSELF:
// 427d027915dba4eba418166eae35d1c24d1b876957dcae1428081bde6f1cdf5c.
// The game packs both eyes into one surface. Double the dimensions selected by
// the original Base/Neo path, and retain that selection for its video pool too.
struct HdProfile {
    u32 native_stereo_width;
    u32 native_stereo_height;
    u32 native_video_pool_mib;
    u32 stereo_width;
    u32 stereo_height;
    u32 video_pool_mib;
    s32 extra_memory_mib;
};

inline constexpr std::optional<HdProfile> DoubleNative(u32 width, u32 height, u32 pool_mib) {
    // Render rectangles hold signed dimensions; pool MOVs and GNM sizes are u32.
    constexpr u32 MaxDimension = std::numeric_limits<s32>::max();
    constexpr u32 MaxPoolMiB = std::numeric_limits<u32>::max() >> 20;
    if (width == 0 || height == 0 || pool_mib == 0 || width % 2 != 0 ||
        width > MaxDimension / 2 || height > MaxDimension / 2) {
        return std::nullopt;
    }
    const u64 hd_pool_mib = (u64{pool_mib} * 44 + 9) / 10; // Four times the pixels, +10%.
    if (hd_pool_mib > MaxPoolMiB) {
        return std::nullopt;
    }
    const u64 extra_mib = ((hd_pool_mib - pool_mib + 255) / 256) * 256;
    return HdProfile{width, height, pool_mib, width * 2, height * 2,
                     static_cast<u32>(hd_pool_mib), static_cast<s32>(extra_mib)};
}

inline constexpr HdProfile Base = *DoubleNative(2304, 1296, 615);
inline constexpr HdProfile Neo = *DoubleNative(2688, 1512, 815);

// Flat 4K mode keeps the same angular rectangle as the current 16:9 flat-view crop,
// but renders that rectangle directly at 3840x2160 for each eye. The packed stereo
// target is 7680x2160. Scale each console's pool by target pixel area and retain 10%
// headroom; GNM receives this size as an unsigned 32-bit byte count.
inline constexpr std::optional<HdProfile> Flat4kProfile(u32 native_width, u32 native_height,
                                                       u32 native_pool_mib) {
    constexpr u32 StereoWidth = 7680;
    constexpr u32 StereoHeight = 2160;
    constexpr u32 MaxPoolMiB = std::numeric_limits<u32>::max() >> 20;
    if (native_width == 0 || native_height == 0 || native_pool_mib == 0 ||
        native_width % 2 != 0 || StereoWidth > static_cast<u32>(std::numeric_limits<s32>::max())) {
        return std::nullopt;
    }
    const u64 native_pixels = u64{native_width} * native_height;
    const u64 target_pixels = u64{StereoWidth} * StereoHeight;
    const u64 pool_mib = (u64{native_pool_mib} * target_pixels * 110 +
                          native_pixels * 100 - 1) / (native_pixels * 100);
    if (pool_mib > MaxPoolMiB || pool_mib < native_pool_mib) return std::nullopt;
    const u64 extra_mib = ((pool_mib - native_pool_mib + 255) / 256) * 256;
    return HdProfile{native_width, native_height, native_pool_mib, StereoWidth, StereoHeight,
                     static_cast<u32>(pool_mib), static_cast<s32>(extra_mib)};
}

inline constexpr HdProfile BaseFlat4k = *Flat4kProfile(2304, 1296, 615);
inline constexpr HdProfile NeoFlat4k = *Flat4kProfile(2688, 1512, 815);

static_assert(BaseFlat4k.stereo_width == 7680 && BaseFlat4k.stereo_height == 2160 &&
              BaseFlat4k.video_pool_mib == 3759 && BaseFlat4k.extra_memory_mib == 3328);
static_assert(NeoFlat4k.stereo_width == 7680 && NeoFlat4k.stereo_height == 2160 &&
              NeoFlat4k.video_pool_mib == 3660 && NeoFlat4k.extra_memory_mib == 3072);
static_assert((u64{BaseFlat4k.video_pool_mib} << 20) <= std::numeric_limits<u32>::max() &&
              (u64{NeoFlat4k.video_pool_mib} << 20) <= std::numeric_limits<u32>::max());

inline constexpr const HdProfile& Flat4kProfileFor(bool neo) {
    return neo ? NeoFlat4k : BaseFlat4k;
}

inline constexpr const HdProfile& ProfileFor(bool neo) {
    return neo ? Neo : Base;
}

inline constexpr u32 kMinScreenPercent = 25;
inline constexpr u32 kMaxScreenPercent = 500;

inline constexpr std::optional<HdProfile> ScaleNative(u32 width, u32 height, u32 pool_mib,
                                                     u32 percent) {
    constexpr u32 MaxDimension = std::numeric_limits<s32>::max();
    constexpr u32 MaxPoolMiB = std::numeric_limits<u32>::max() >> 20;
    if (percent < kMinScreenPercent || percent > kMaxScreenPercent || width == 0 ||
        height == 0 || pool_mib == 0 || width % 2 != 0) {
        return std::nullopt;
    }
    if (percent == 200) return DoubleNative(width, height, pool_mib);
    // Each eye and the height are aligned to eight pixels. Keep the original pool
    // below native resolution: the title also allocates fixed-size resources there.
    // Above ~200% the area-scaled pool can exceed GNM's u32 byte budget; clamp it.
    const u64 scaled_width_u = (u64{width / 2} * percent + 799) / 800 * 16;
    const u64 scaled_height_u = (u64{height} * percent + 799) / 800 * 8;
    if (scaled_width_u == 0 || scaled_height_u == 0 || scaled_width_u > MaxDimension ||
        scaled_height_u > MaxDimension) {
        return std::nullopt;
    }
    const u32 scaled_width = static_cast<u32>(scaled_width_u);
    const u32 scaled_height = static_cast<u32>(scaled_height_u);
    u32 scaled_pool = pool_mib;
    if (percent > 100) {
        const u64 raw_pool = (u64{pool_mib} * percent * percent * 11 + 99999) / 100000;
        scaled_pool = raw_pool > MaxPoolMiB ? MaxPoolMiB : static_cast<u32>(raw_pool);
        if (scaled_pool < pool_mib) {
            scaled_pool = pool_mib;
        }
    }
    const s32 extra = static_cast<s32>((u64{scaled_pool - pool_mib} + 255) / 256 * 256);
    return HdProfile{width, height, pool_mib, scaled_width, scaled_height, scaled_pool, extra};
}

inline constexpr HdProfile ProfileForPercent(bool neo, u32 percent) {
    const auto& native = ProfileFor(neo);
    return *ScaleNative(native.native_stereo_width, native.native_stereo_height,
                        native.native_video_pool_mib, percent);
}

inline constexpr std::array<Builds::Change, 6> Changes{{
    {0x1a09108, Base.native_stereo_width, Base.stereo_width, 4},
    {0x1a0910c, Base.native_stereo_height, Base.stereo_height, 4},
    {0x1a09128, Neo.native_stereo_width, Neo.stereo_width, 4},
    {0x1a0912c, Neo.native_stereo_height, Neo.stereo_height, 4},
    // mov edx,815 MiB; mov ecx,615 MiB; cmovg rcx,rdx. These MOVs zero-extend
    // the pool size; the game's GNM allocator compares its u32 sizes unsigned.
    {0x1999a5, u64{Neo.native_video_pool_mib} << 20, u64{Neo.video_pool_mib} << 20, 4},
    {0x1999aa, u64{Base.native_video_pool_mib} << 20, u64{Base.video_pool_mib} << 20, 4},
}};

inline constexpr std::array<Builds::Change, 6> Flat4kChanges{{
    {0x1a09108, Base.native_stereo_width, BaseFlat4k.stereo_width, 4},
    {0x1a0910c, Base.native_stereo_height, BaseFlat4k.stereo_height, 4},
    {0x1a09128, Neo.native_stereo_width, NeoFlat4k.stereo_width, 4},
    {0x1a0912c, Neo.native_stereo_height, NeoFlat4k.stereo_height, 4},
    {0x1999a5, u64{Neo.native_video_pool_mib} << 20, u64{NeoFlat4k.video_pool_mib} << 20, 4},
    {0x1999aa, u64{Base.native_video_pool_mib} << 20, u64{BaseFlat4k.video_pool_mib} << 20, 4},
}};

struct Fingerprint {
    u64 at;
    std::span<const u8> bytes;
};

inline constexpr std::array<u8, 16> RenderSetup{
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x53, 0x48, 0x81, 0xec, 0xf8, 0x00, 0x00, 0x00};
inline constexpr std::array<u8, 16> PoolSetup{
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec};
inline constexpr std::array<u8, 8> BaseTableRead{
    0xc5, 0xfc, 0x28, 0x15, 0x99, 0x59, 0x26, 0x01};
inline constexpr std::array<u8, 8> NeoTableRead{
    0xc5, 0xfc, 0x28, 0x0d, 0xb1, 0x59, 0x26, 0x01};
inline constexpr std::array<u8, 20> SceneSizeCopy{
    0x41, 0x8b, 0x47, 0x08, 0x89, 0x83, 0x68, 0x4b, 0x00, 0x00,
    0x41, 0x8b, 0x4f, 0x0c, 0x89, 0x8b, 0x6c, 0x4b, 0x00, 0x00};
inline constexpr std::array<u8, 1> MovEdx{0xba};
inline constexpr std::array<u8, 1> MovEcx{0xb9};
inline constexpr std::array<u8, 4> SelectPool{0x48, 0x0f, 0x4f, 0xca};
inline constexpr std::array<Fingerprint, 8> Fingerprints{{
    {0x7a3740, RenderSetup}, {0x199900, PoolSetup},
    {0x7a375f, BaseTableRead}, {0x7a3767, NeoTableRead},
    {0xf7f4d, SceneSizeCopy}, {0x1999a4, MovEdx},
    {0x1999a9, MovEcx}, {0x1999b0, SelectPool},
}};

inline bool Matches(std::span<const u8> image, std::string_view serial) {
    if (serial != "CUSA02350") return false;
    for (const auto& fingerprint : Fingerprints) {
        if (fingerprint.at > image.size() ||
            image.size() - fingerprint.at < fingerprint.bytes.size() ||
            std::memcmp(image.data() + fingerprint.at, fingerprint.bytes.data(),
                        fingerprint.bytes.size()) != 0) {
            return false;
        }
    }
    return Builds::FirstUnexpected(image, Changes) == nullptr;
}

// Validate every instruction and original value before any write. An unsupported or
// already-modified image cannot receive a partial HD patch.
inline bool Apply(std::span<u8> image, std::string_view serial) {
    return Matches(image, serial) && Builds::Apply(image, Changes) == nullptr;
}

// The end callback (0xeba30) is stubbed by HMD HLE, so its counter stays zero.
// The fallback at 0xebb10 then waits three vblanks instead of two callbacks.
// At an explicitly selected target rate (36/72/90/120 Hz), retain the first event wait but leave the
// loop immediately afterwards. GPU frame-label ownership remains intact. The clock conversion correction
// below is also required before increasing the presentation rate.
inline constexpr std::array<u8, 61> FrameWaitCode{
    0x49,0x8b,0xbd,0x90,0x14,0x00,0x00,0xba,0x01,0x00,0x00,0x00,
    0x45,0x31,0xc0,0x48,0x8d,0x74,0x24,0x60,0x48,0x8d,0x4c,0x24,
    0x28,0xe8,0x9f,0x40,0x6c,0x01,0x83,0xfb,0x01,0x74,0x1a,0x41,
    0x83,0xfe,0x01,0x7f,0x14,0x31,0xc0,0x31,0xc9,0x41,0xff,0xc6,
    0xf0,0x41,0x0f,0xb1,0x0c,0x24,0x89,0xc3,0x83,0xfb,0x01,0x7e,
    0x98};
inline constexpr std::array<Builds::Change, 1> PaceChanges{{{0xebb5c,0x74,0xeb,1}}};

inline bool PaceMatches(std::span<const u8> image) {
    return image.size() >= 0xebb3b + FrameWaitCode.size() &&
           std::memcmp(image.data() + 0xebb3b, FrameWaitCode.data(), FrameWaitCode.size()) == 0;
}

// Validate both requested features before either writes to the image.
inline constexpr std::array<Builds::Change, 7> ClockChanges{{
    {0x19edca0,0x3ea50ceae0000000ULL,0,8},
    {0x19edca8,0x3e058e4060000000ULL,0,8},
    {0x19edcb0,0x3ea50ceae0000000ULL,0,8},
    {0x19edcb8,0x3e058e4060000000ULL,0,8},
    {0x8e6ce5,0x09804040,0,4},
    {0x8e6cfc,0x09804040,0,4},
    {0x8e6d23,0x09804040,0,4},
}};

inline constexpr std::array<u8, 21> ClockUpdateCode{
    0x89,0xf0,0xff,0x47,0x14,0xc5,0xfb,0x10,0x0d,0x73,0x4a,0x50,0x01,
    0xc5,0xfb,0x10,0x1d,0x73,0x4a,0x50,0x01};
inline constexpr std::array<u8, 18> ClockReadCode{
    0x0f,0x31,0xc5,0xfb,0x10,0x0d,0x06,0x4b,0x50,0x01,
    0xc5,0xfb,0x10,0x1d,0x06,0x4b,0x50,0x01};

inline constexpr std::array<u8, 81> ClockClampCode{
    0x0f, 0x31, 0x41, 0xbe, 0x40, 0x40, 0x80, 0x09, 0x4c, 0x89, 0xef, 0x89,
    0xc0, 0x48, 0xc1, 0xe2, 0x20, 0x48, 0x09, 0xc2, 0x48, 0x2b, 0x53, 0x48,
    0x48, 0x81, 0xfa, 0x40, 0x40, 0x80, 0x09, 0x41, 0x0f, 0x43, 0xd6, 0x89,
    0xd6, 0xe8, 0x25, 0x25, 0xc0, 0xff, 0x0f, 0x31, 0x4c, 0x89, 0xe7, 0x89,
    0xc0, 0x48, 0xc1, 0xe2, 0x20, 0x48, 0x09, 0xc2, 0x48, 0x2b, 0x93, 0x80,
    0x00, 0x00, 0x00, 0x48, 0x81, 0xfa, 0x40, 0x40, 0x80, 0x09, 0x41, 0x0f,
    0x43, 0xd6, 0x89, 0xd6, 0xe8, 0xfe, 0x24, 0xc0, 0xff};

inline bool ClockMatches(std::span<const u8> image) {
    return image.size() >= 0x19edcc0 &&
           std::memcmp(image.data()+0x4e9230, ClockUpdateCode.data(), ClockUpdateCode.size()) == 0 &&
           std::memcmp(image.data()+0x4e9190, ClockReadCode.data(), ClockReadCode.size()) == 0 &&
           std::memcmp(image.data()+0x8e6ce1, ClockClampCode.data(), ClockClampCode.size()) == 0 &&
           Builds::FirstUnexpected(image, ClockChanges) == nullptr;
}

// Rush of Blood reads the host RDTSC directly but converts its ticks using a
// hardcoded PS4 frequency (1594 MHz). All four conversion constants and the
// two capped clock deltas must use the actual host frequency together.
inline bool ApplyOptions(std::span<u8> image, std::string_view serial, bool hd, bool pace,
                         u64 host_tsc_hz = 1594000000, u32 percent = 200,
                         bool flat4k = false) {
    if (hd && !flat4k &&
        (percent < kMinScreenPercent || percent > kMaxScreenPercent)) {
        return false;
    }
    if (!Matches(image, serial) ||
        (pace && (!PaceMatches(image) || !ClockMatches(image) ||
                    host_tsc_hz < 100000000 || host_tsc_hz > 10000000000ULL))) return false;
    auto clocks = ClockChanges;
    if (pace) {
        for (size_t i=0; i<4; ++i) {
            clocks[i].now = std::bit_cast<u64>((i % 2 == 0 ? 1e6 : 1.0) / host_tsc_hz);
        }
        for (size_t i=4; i<clocks.size(); ++i) clocks[i].now = host_tsc_hz / 10;
    }
    if (hd) {
        if (flat4k) {
            Builds::Apply(image, Flat4kChanges);
        } else {
            auto scaled = Changes;
            const auto base = ProfileForPercent(false, percent);
            const auto neo = ProfileForPercent(true, percent);
            scaled[0].now = base.stereo_width;
            scaled[1].now = base.stereo_height;
            scaled[2].now = neo.stereo_width;
            scaled[3].now = neo.stereo_height;
            scaled[4].now = u64{neo.video_pool_mib} << 20;
            scaled[5].now = u64{base.video_pool_mib} << 20;
            Builds::Apply(image, scaled);
        }
    }
    if (pace) Builds::Apply(image, clocks);
    if (pace) Builds::Apply(image, PaceChanges);
    return true;
}

} // namespace Core::KnownTitle::RobBuilds
