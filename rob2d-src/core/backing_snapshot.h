// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Core {
// MemoryManager additionally holds its shared memory lock during capture and copy,
// and its writer lock during invalidation, making validation and copying atomic
// with respect to replacement of the guest mapping.
class BackingSnapshots {
public:
    struct Segment {
        uint64_t address;
        uint64_t physical;
        uint64_t size;
        bool operator==(const Segment&) const = default;
    };
    struct Snapshot {
        uint64_t address;
        uint64_t size;
        std::vector<Segment> segments;
        bool valid = true; // Guarded by the registry mutex.
    };
    using Handle = std::shared_ptr<Snapshot>;

    static void Append(std::vector<Segment>& segments, Segment segment) {
        if (!segments.empty() && segments.back().address + segments.back().size == segment.address &&
            segments.back().physical + segments.back().size == segment.physical) {
            segments.back().size += segment.size;
        } else {
            segments.push_back(segment);
        }
    }

    Handle Capture(uint64_t address, uint64_t size, std::vector<Segment> segments) {
        auto handle = std::make_shared<Snapshot>(Snapshot{address, size, std::move(segments)});
        std::scoped_lock lk{mutex};
        std::erase_if(pending, [](const auto& entry) { return entry.expired(); });
        pending.emplace_back(handle);
        return handle;
    }

    void Invalidate(uint64_t address, uint64_t size) {
        std::scoped_lock lk{mutex};
        for (const auto& entry : pending) {
            if (auto snapshot = entry.lock(); snapshot && Overlaps(*snapshot, address, size)) {
                snapshot->valid = false;
            }
        }
    }

    bool HasPending(uint64_t address, uint64_t size) {
        std::scoped_lock lk{mutex};
        return std::any_of(pending.begin(), pending.end(), [&](const auto& entry) {
            auto snapshot = entry.lock();
            return snapshot && snapshot->valid && Overlaps(*snapshot, address, size);
        });
    }

    template <typename Copy>
    bool Write(const Handle& snapshot, const std::vector<Segment>& current, Copy&& copy) {
        std::scoped_lock lk{mutex};
        if (!snapshot || !snapshot->valid || snapshot->segments != current) {
            return false;
        }
        copy();
        return true;
    }

private:
    static bool Overlaps(const Snapshot& snapshot, uint64_t address, uint64_t size) {
        // Subtraction avoids endpoint overflow and excludes empty ranges.
        return size != 0 && snapshot.size != 0 &&
               (snapshot.address <= address ? address - snapshot.address < snapshot.size
                                            : snapshot.address - address < size);
    }
    std::mutex mutex;
    std::vector<std::weak_ptr<Snapshot>> pending;
};
} // namespace Core
