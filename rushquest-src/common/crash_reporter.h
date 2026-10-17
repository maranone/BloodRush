// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Common {

/// Call once at startup (from SignalDispatch ctor). Checks BACHATA_CRASH_REGISTERS.
void InitCrashReporter();

/// Async-signal-safe: dumps registers + fault info to stderr.
/// Must only be called from a signal handler where raw_context is valid.
void ReportCrash(void* raw_context, int signum, void* siginfo);

/// Record a fatal host failure and write a full Windows process dump when enabled.
void WriteCrashDump(const char* reason, void* exception_pointers = nullptr);

/// Keep optional stream-file misses out of the error log; summarize them in a fatal report.
void RecordCorestreamMiss(const char* guest_path);

} // namespace Common
