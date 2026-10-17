// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

union SDL_Event;

namespace Input::DesktopPlay {

/// A PlayStation VR title played on a monitor with keyboard and mouse (SHADPS4_DESKTOP_PLAY=1):
/// the mouse turns the head and both Move controllers point where the head looks, at the middle
/// of the screen. Q / E lean left / right, both (or C) crouch. Left click pulls both triggers,
/// right click presses both Move buttons, middle click flicks both guns. Space is Cross,
/// Backspace Circle, T Triangle, X Square, Enter START, R resets the view, Z the gamepad's Cross.
/// SHADPS4_DESKTOP_SENS=<degrees per mouse count> (0.08).
bool Enabled();

/// Starts the thread that feeds the headset and controllers.
void Start();

/// Takes the window's keyboard and mouse events while enabled: none of them reaches the
/// emulator's own keyboard bindings, which would act as the gamepad (and a title playing with
/// Move controllers pauses for a gamepad picked up). Function keys go on. True if consumed.
bool OnEvent(const SDL_Event& event);

} // namespace Input::DesktopPlay
