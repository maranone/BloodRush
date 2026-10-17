// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdlib>
#include <string_view>

namespace Core::RobQuality {
enum class Level { UltraLow, Low, Medium, High };

struct Profile {
    Level level;
    // Zero retains the game's requested anisotropy.
    float max_anisotropy;
    // Added to the guest sampler LOD bias (positive → smaller mips).
    float lod_bias_add;
    // Raised floor on sampler min LOD in mip levels (positive → skip larger mips).
    float min_lod_add;
    // Upload staging ring size in MiB; 0 keeps the Bachata/desktop default.
    unsigned staging_mb;
    // Texture-cache GC thresholds as percent of device budget; 0 keeps defaults.
    unsigned gc_pressure_pct;
    unsigned gc_critical_pct;
    // Quest RAM saver: tighter texture GC thresholds + smaller staging (no tiled FreeImage).
    bool ram_saver;
};

constexpr Level Parse(std::string_view name) {
    if (name == "ultralow" || name == "ultra" || name == "ultra-low") {
        return Level::UltraLow;
    }
    if (name == "low") {
        return Level::Low;
    }
    if (name == "medium") {
        return Level::Medium;
    }
    return Level::High;
}

constexpr std::string_view Name(Level level) {
    switch (level) {
    case Level::UltraLow:
        return "ultralow";
    case Level::Low:
        return "low";
    case Level::Medium:
        return "medium";
    default:
        return "high";
    }
}

constexpr Profile For(Level level) {
    switch (level) {
    case Level::UltraLow:
        // Stronger mip selection + host RAM saver (staging / tight GC).
        return {level, 1.f, 2.f, 1.f, 16u, 25u, 40u, true};
    case Level::Low:
        return {level, 1.f, 0.f, 0.f, 0u, 0u, 0u, false};
    case Level::Medium:
        return {level, 2.f, 0.f, 0.f, 0u, 0u, 0u, false};
    default:
        return {level, 0.f, 0.f, 0.f, 0u, 0u, 0u, false};
    }
}

inline Level RequestedLevel() {
    const char* value = std::getenv("SHADPS4_ROB_QUALITY");
    return Parse(value ? std::string_view{value} : std::string_view{});
}

// A profile becomes active only after the supported game build was verified.
// Selection is fixed for a launched session, so existing sampler cache keys remain valid.
inline std::atomic<Level> active_level{Level::High};
inline void Activate(Level level) { active_level.store(level, std::memory_order_release); }
inline Profile Active() { return For(active_level.load(std::memory_order_acquire)); }

// Profile implied by the process env before Activate() (constructors / staging).
inline Profile Requested() { return For(RequestedLevel()); }

constexpr float CapAnisotropy(float requested, Profile profile) {
    return profile.max_anisotropy > 0 && requested > profile.max_anisotropy
               ? profile.max_anisotropy : requested;
}
} // namespace Core::RobQuality
