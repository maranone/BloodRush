// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string_view>
#include "common/types.h"
#include "core/rob_quality.h"
#include "core/known_title_rob_quality_layout.h"

namespace Core::KnownTitle::RobQualityBuilds {

// CUSA02350 1.00. Module offsets, never file offsets. The original executable
// SHA256 and the inspected engine paths are recorded in rea/gh/out/quality-controls.md.
// These are original engine resource switches; no renderer draw suppression is used.
inline constexpr u64 PostEffects = 0x2222f9c;
inline constexpr std::array<u8, 4> OriginalEffects{0, 0x86, 0, 0};
inline constexpr u64 LightResourceCastShadows = 0x379008;
inline constexpr std::array<u8, 10> OriginalCastShadows{
    0x8a, 0x01, 0x41, 0x88, 0x84, 0x24, 0x14, 0x01, 0x00, 0x00};
inline constexpr std::array<u8, 10> NoCastShadows{
    0x30, 0xc0, 0x41, 0x88, 0x84, 0x24, 0x14, 0x01, 0x00, 0x00};

// Enforce the original object switch again where the frame's lighting buckets
// are classified. Animation/gameplay can change the instance after resource load.
// Both paths store zero to the current instance and retain the original false
// classification. Only BL/R9B change, exactly as in the original readers.
inline constexpr u64 ClassifyLight = 0x3c4404;
inline constexpr std::array<u8, 12> OriginalClassifyLight{
    0x8a,0x9b,0x14,0x01,0,0,0x84,0xdb,0x41,0x0f,0x95,0xc1};
inline constexpr std::array<u8, 12> NoShadowClassifyLight{
    0xc6,0x83,0x14,0x01,0,0,0,0x30,0xdb,0x41,0xb1,0};
inline constexpr u64 ClassifySpot = 0x3c442c;
inline constexpr std::array<u8, 11> OriginalClassifySpot{
    0x80,0xbb,0x14,0x01,0,0,0,0x41,0x0f,0x95,0xc1};
inline constexpr std::array<u8, 11> NoShadowClassifySpot{
    0xc6,0x83,0x14,0x01,0,0,0,0x41,0xb1,0,0x90};

struct FlagConsumer {
    u64 at;
    std::span<const u8> original;
    std::span<const u8> disabled;
};
// These comparisons are exclusively the same CastShadows switch. Comparing a
// register with itself supplies the exact flags for an original zero switch,
// without changing any register or skipping unrelated lighting/geometry work.
inline constexpr std::array<u8, 8> OriginalR13Gate{
    0x41,0x80,0xbd,0x14,0x01,0,0,0};
inline constexpr std::array<u8, 8> DisabledR13Gate{
    0x4d,0x39,0xed,0x90,0x90,0x90,0x90,0x90};
inline constexpr std::array<u8, 9> OriginalR12Gate{
    0x41,0x80,0xbc,0x24,0x14,0x01,0,0,0};
inline constexpr std::array<u8, 9> DisabledR12Gate{
    0x4d,0x39,0xe4,0x90,0x90,0x90,0x90,0x90,0x90};
inline constexpr std::array<FlagConsumer, 4> ShadowFlagConsumers{{
    {0x382b80, OriginalR13Gate, DisabledR13Gate},
    {0x383a49, OriginalR12Gate, DisabledR12Gate},
    {0x3851fb, OriginalR13Gate, DisabledR13Gate},
    // SMG's frame shadow-list builder retains its -1 shadow-index fallback for
    // every light when this flag is false. The light itself remains in its
    // lighting list and its GPU light record receives that -1 shadow index.
    {0x43b4c4, OriginalR12Gate, DisabledR12Gate},
}};

// LightShadowed and Sun resource-copy implementations both transfer the scalar
// ShadowMapSize (resource+0xe0) to the instance+0x1b0. The adjacent fields are
// copied unchanged. Addressing them through a scratch RCX makes room to halve
// positive sizes and preserve every nonpositive value (including sentinels),
// without a trampoline or a new allocation. Neither path uses the previous
// RCX after this block; their later calls assign their own argument registers.
inline constexpr std::array<u64, 2> ShadowSizeCopies{0x3f05db, 0x3f07a2};
inline constexpr std::array<u8, 73> OriginalShadowSize{
    0x8b,0x83,0xe0,0,0,0,0x41,0x89,0x86,0xb0,0x01,0,0,
    0x8b,0x83,0xf0,0,0,0,0x41,0x89,0x86,0xc0,0x01,0,0,
    0x8b,0x83,0xf4,0,0,0,0x41,0x89,0x86,0xc4,0x01,0,0,
    0xc5,0xf8,0x28,0x83,0x00,0x01,0,0,0xc4,0xc1,0x78,0x29,0x86,0xd0,0x01,0,0,
    0xc5,0xf8,0x28,0x83,0x10,0x01,0,0,0xc4,0xc1,0x78,0x29,0x86,0xe0,0x01,0,0};
inline constexpr std::array<u8, 73> HalfShadowSize{
    0x48,0x8d,0x8b,0xe0,0,0,0,0x8b,0x01,0x85,0xc0,0x7e,0x02,0xd1,0xf8,
    0x41,0x89,0x86,0xb0,0x01,0,0,
    0x8b,0x41,0x10,0x41,0x89,0x86,0xc0,0x01,0,0,
    0x8b,0x41,0x14,0x41,0x89,0x86,0xc4,0x01,0,0,
    0xc5,0xf8,0x28,0x41,0x20,0xc4,0xc1,0x78,0x29,0x86,0xd0,0x01,0,0,
    0xc5,0xf8,0x28,0x41,0x30,0xc4,0xc1,0x78,0x29,0x86,0xe0,0x01,0,0,
    0x90,0x90,0x90};

inline bool BytesMatch(std::span<const u8> image, u64 at, std::span<const u8> bytes) {
    return at <= image.size() && bytes.size() <= image.size() - at &&
           std::memcmp(image.data() + at, bytes.data(), bytes.size()) == 0;
}

inline bool Matches(std::span<const u8> image, std::string_view serial) {
    if (serial != "CUSA02350" || !BytesMatch(image, PostEffects, OriginalEffects) ||
        !BytesMatch(image, LightResourceCastShadows, OriginalCastShadows) ||
        !AtlasLayoutMatches(image)) return false;
    for (const u64 at : ShadowSizeCopies) {
        if (!BytesMatch(image, at, OriginalShadowSize)) return false;
    }
    if (!BytesMatch(image, ClassifyLight, OriginalClassifyLight) ||
        !BytesMatch(image, ClassifySpot, OriginalClassifySpot)) return false;
    for (const auto& consumer : ShadowFlagConsumers) {
        if (!BytesMatch(image, consumer.at, consumer.original)) return false;
    }
    // The light flag must still drive the engine's lighting classification and
    // shadowed draw path. Unsupported builds fail before any mutation.
    constexpr std::array<u8, 9> Classification{
        0x8a,0x9b,0x14,0x01,0,0,0x84,0xdb,0x41};
    constexpr std::array<u8, 8> ShadowGate{
        0x41,0x80,0xbd,0x14,0x01,0,0,0};
    return BytesMatch(image, 0x3c4404, Classification) &&
           BytesMatch(image, 0x382b80, ShadowGate);
}

inline void WritePostMask(std::span<u8> image, u32 mask) {
    std::memcpy(image.data() + PostEffects, &mask, sizeof(mask));
}

inline u32 ReadPostMask(std::span<const u8> image) {
    u32 mask{};
    std::memcpy(&mask, image.data() + PostEffects, sizeof(mask));
    return mask;
}

// EPostEffect bit VALUE 0x8000 is EyeDistortion. Stock mask is 0x00008600; motion
// blur (1) and bloom (4) are already absent. All consumers, including final
// composition, use this same static mask.
inline constexpr u32 EyeDistortionBit = 0x8000;

inline void ApplyHalfShadows(std::span<u8> image) {
    // SMG uses its own count-indexed atlas layouts. Modify the original
    // initialization inputs before guest constructors execute: normalized
    // sampling coordinates and integer viewport bounds are scaled together.
    // The 1024 atlas dimensions and the scene resolution remain unchanged.
    ApplyHalfAtlasLayout(image);
    for (const u64 at : ShadowSizeCopies) {
        std::copy(HalfShadowSize.begin(), HalfShadowSize.end(), image.begin() + at);
    }
}

inline void ApplyNoShadows(std::span<u8> image) {
    std::copy(NoCastShadows.begin(), NoCastShadows.end(),
              image.begin() + LightResourceCastShadows);
    std::copy(NoShadowClassifyLight.begin(), NoShadowClassifyLight.end(),
              image.begin() + ClassifyLight);
    std::copy(NoShadowClassifySpot.begin(), NoShadowClassifySpot.end(),
              image.begin() + ClassifySpot);
    for (const auto& consumer : ShadowFlagConsumers) {
        std::copy(consumer.disabled.begin(), consumer.disabled.end(),
                  image.begin() + consumer.at);
    }
}

inline bool Apply(std::span<u8> image, std::string_view serial, RobQuality::Level level) {
    // Validate all sites even for High, so the caller can expose support accurately.
    if (!Matches(image, serial)) return false;
    if (level == RobQuality::Level::UltraLow || level == RobQuality::Level::Low ||
        level == RobQuality::Level::Medium) {
        // Half shadow sizes + EyeDistortion clear for Low/UltraLow/Medium.
        // ApplyNoShadows (CastShadows kill + classify/consumers) stays out: it
        // corrupted end-of-frame GPU submit (guest 0xfa9b0/0xfa210). Ultra Low
        // also uses host LOD + a smaller staging ring / tighter texture GC.
        ApplyHalfShadows(image);
        WritePostMask(image, ReadPostMask(image) & ~EyeDistortionBit);
    }
    return true;
}

} // namespace Core::KnownTitle::RobQualityBuilds
