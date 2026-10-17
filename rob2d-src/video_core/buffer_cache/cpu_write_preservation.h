// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace VideoCore {
// Decide under the tracker lock, but perform the requested download after releasing
// it. A page containing GPU data must remain protected until that data is restored;
// DownloadBufferMemory subsequently marks the CPU write's page dirty.
template <typename MarkCpuDirty>
bool PrepareCpuWrite(bool readbacks_enabled, bool preserve_gpu_writes, bool gpu_modified,
                     MarkCpuDirty&& mark_cpu_dirty) {
    if ((readbacks_enabled || preserve_gpu_writes) && gpu_modified) {
        return true;
    }
    mark_cpu_dirty();
    return false;
}
} // namespace VideoCore
