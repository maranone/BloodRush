// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace Core::RobMotionTelemetry {

/// Sample Rush of Blood cart pose/speed and UDP-feed BloodRush_telemetry (127.0.0.1:25151).
/// No-op unless CUSA02350 and enabled (default on; set SHADPS4_ROB_MOTION_TELEMETRY=0 to disable).
void OnFrame(VAddr eboot_base);

} // namespace Core::RobMotionTelemetry
