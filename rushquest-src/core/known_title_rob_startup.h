// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdlib>
#include <span>
#include <string_view>
#include "core/known_title_builds.h"
#include "core/known_title_rob.h"

/// Rush of Blood (CUSA02350 1.00): the controller chooser and first-time calibration screens are
/// not wanted with an emulated pair of Moves, whose poses come from the headset (or from the
/// keyboard in 2D mode) and need no calibration by the player.
///
/// Both changes are made to the loaded image before any of it runs, so they hold for guest code
/// under native execution and under FEX alike, and are written only if every site is as the
/// inspected build has it. SHADPS4_ROB_STARTUP=0 leaves the title's own startup flow alone.
namespace Core::KnownTitle::RobBuilds {

// Two Moves (control scheme 3) whatever the pad library says is connected. The player's input
// setup (0xcfa090) and its re-detection (0xcfa1a0) choose the scheme: 3 if both Moves report as
// connected, else whatever 0x25e190 finds. Both end in 0xcfdeb0, which registers the tracker
// handles and sets the input mode, so the game does what it does when it finds two Moves.
//   je  <other schemes>  -> nop nop      (the first Move need not report as connected)
//   jne <esi = 3 is used> -> jmp         (nor the second)
// With the scheme never 0, the chooser the frontend shows for a scheme of 0 (0xc84e20) is gone.
//
// The first-time flow (0xc85870) is controller chooser, CalibrationPanel, then
// FirstTimePostCalibrationPanel. Its chooser call (0xc8a330) is made a nop; the calibration
// is not skipped, as what it records (where the hands are held, how far they reach) is what
// places the guns: the player does it. The options menu's own entries for choosing a controller and calibrating again (0xc89a80) are kept.
inline constexpr std::array<Builds::Change, 5> StartupChanges{{
    {0xbfa15a, 0x1774, 0x9090, 2},
    {0xbfa171, 0x0c75, 0x0ceb, 2},
    {0xbfa1be, 0x1774, 0x9090, 2},
    {0xbfa1d5, 0x0c75, 0x0ceb, 2},
    // call 0xc8a330  ->  5-byte nop
    {0xb8597d, 0x00000049aee8ULL, 0x000000441f0fULL, 5},
}};

inline bool StartupRequested() {
    const char* value = std::getenv("SHADPS4_ROB_STARTUP");
    return value == nullptr || std::string_view{value} != "0";
}

/// Whether the image is the build the startup changes were made for, and has not been changed
/// in those places.
inline bool StartupMatches(std::span<const u8> image, std::string_view serial) {
    return Matches(image, serial) && Builds::FirstUnexpected(image, StartupChanges) == nullptr;
}

inline bool ApplyStartup(std::span<u8> image, std::string_view serial) {
    return StartupMatches(image, serial) && Builds::Apply(image, StartupChanges) == nullptr;
}

} // namespace Core::KnownTitle::RobBuilds
