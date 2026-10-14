// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string_view>

#include "common/elf_info.h"
#include "common/logging/log.h"
#include "core/memory.h"
#include "core/rob_motion_telemetry.h"

#ifdef _WIN32
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Core::RobMotionTelemetry {
namespace {

// Module vaddrs from RE of CUSA02350 1.00 (rob.elf). Guest = eboot_base + vaddr.
constexpr u64 kPlayerMgrPtr = 0x1fd23d0; // -> mgr; player = *(mgr + 0x10)
constexpr u64 kPlayerCompCount = 0x28;   // i32
constexpr u64 kPlayerCompArray = 0x30;   // ptr to component*
constexpr u64 kVehicleLink = 0x2a8;      // ROBVehicleComponent -> train object
constexpr u64 kTrainParent = 0x680;      // walk to root train
constexpr u64 kTrainState = 0x32f8;      // i32 (2 = stopped)
constexpr u64 kTrainSpeed = 0x32fc;      // f32 m/s
constexpr u64 kTrainBody = 0x2a0;        // physics / scene body*
constexpr u64 kBodyPos = 0x148;          // double[3]
constexpr u64 kBodyRot = 0x160;          // 9×f32 Mat3 (last float at +0x180)

constexpr u16 kFeedPort = 25151;
constexpr size_t kPacketSize = 52; // 11×f32 + 2×u32

struct Sample {
    float speed{};
    float pos_x{}, pos_y{}, pos_z{};
    float quat_x{}, quat_y{}, quat_z{}, quat_w{1.f};
    float gforce_x{}, gforce_y{1.f}, gforce_z{};
    u32 flags{};
    u32 frame{};
};

bool SafeRead(VAddr address, void* out, size_t size) {
    if (address == 0 || size == 0) {
        return false;
    }
#ifdef _WIN32
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size,
                             &done) &&
           done == size;
#else
    auto* memory = Core::Memory::Instance();
    if (!memory || !memory->IsValidMapping(address, size)) {
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
#endif
}

template <typename T>
bool SafeRead(VAddr address, T& out) {
    return SafeRead(address, &out, sizeof(T));
}

bool Enabled() {
    static const bool enabled = [] {
        if (Common::ElfInfo::Instance().GameSerial() != "CUSA02350") {
            return false;
        }
        const char* value = std::getenv("SHADPS4_ROB_MOTION_TELEMETRY");
        if (value == nullptr) {
            return true;
        }
        return std::string_view{value} != "0";
    }();
    return enabled;
}

VAddr RootTrain(VAddr train) {
    for (int i = 0; i < 16; ++i) {
        VAddr parent = 0;
        if (!SafeRead(train + kTrainParent, parent) || parent == 0) {
            return train;
        }
        train = parent;
    }
    return train;
}

bool LooksLikeTrain(VAddr train) {
    s32 state = -1;
    float speed = 0.f;
    VAddr body = 0;
    if (!SafeRead(train + kTrainState, state) || state < 0 || state > 3) {
        return false;
    }
    if (!SafeRead(train + kTrainSpeed, speed) || !std::isfinite(speed) || std::abs(speed) > 200.f) {
        return false;
    }
    if (!SafeRead(train + kTrainBody, body) || body == 0) {
        return false;
    }
    double x = 0, y = 0, z = 0;
    if (!SafeRead(body + kBodyPos, x) || !SafeRead(body + kBodyPos + 8, y) ||
        !SafeRead(body + kBodyPos + 16, z)) {
        return false;
    }
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

VAddr FindPlayerTrain(VAddr base) {
    VAddr mgr = 0;
    if (!SafeRead(base + kPlayerMgrPtr, mgr) || mgr == 0) {
        return 0;
    }
    VAddr player = 0;
    if (!SafeRead(mgr + 0x10, player) || player == 0) {
        return 0;
    }
    s32 count = 0;
    VAddr array = 0;
    if (!SafeRead(player + kPlayerCompCount, count) || count <= 0 || count > 256) {
        return 0;
    }
    if (!SafeRead(player + kPlayerCompArray, array) || array == 0) {
        return 0;
    }

    VAddr found = 0;
    for (s32 i = 0; i < count; ++i) {
        VAddr comp = 0;
        if (!SafeRead(array + static_cast<u64>(i) * 8, comp) || comp == 0) {
            continue;
        }
        VAddr linked = 0;
        if (!SafeRead(comp + kVehicleLink, linked) || linked == 0) {
            continue;
        }
        if (!LooksLikeTrain(linked)) {
            continue;
        }
        found = RootTrain(linked);
        break;
    }
    return found;
}

void Mat3ToQuat(const float m[9], float& qx, float& qy, float& qz, float& qw) {
    const float tr = m[0] + m[4] + m[8];
    if (tr > 0.f) {
        const float s = std::sqrt(tr + 1.f) * 2.f;
        qw = 0.25f * s;
        qx = (m[5] - m[7]) / s;
        qy = (m[6] - m[2]) / s;
        qz = (m[1] - m[3]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.f + m[0] - m[4] - m[8]) * 2.f;
        qw = (m[5] - m[7]) / s;
        qx = 0.25f * s;
        qy = (m[1] + m[3]) / s;
        qz = (m[6] + m[2]) / s;
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.f + m[4] - m[0] - m[8]) * 2.f;
        qw = (m[6] - m[2]) / s;
        qx = (m[1] + m[3]) / s;
        qy = 0.25f * s;
        qz = (m[5] + m[7]) / s;
    } else {
        const float s = std::sqrt(1.f + m[8] - m[0] - m[4]) * 2.f;
        qw = (m[1] - m[3]) / s;
        qx = (m[6] + m[2]) / s;
        qy = (m[5] + m[7]) / s;
        qz = 0.25f * s;
    }
    const float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (n > 1e-6f) {
        qx /= n;
        qy /= n;
        qz /= n;
        qw /= n;
    } else {
        qx = qy = qz = 0.f;
        qw = 1.f;
    }
}

void WorldToLocal(const float m[9], float wx, float wy, float wz, float& lx, float& ly,
                  float& lz) {
    lx = m[0] * wx + m[3] * wy + m[6] * wz;
    ly = m[1] * wx + m[4] * wy + m[7] * wz;
    lz = m[2] * wx + m[5] * wy + m[8] * wz;
}

bool SampleTrain(VAddr train, Sample& out, double dt) {
    static std::array<double, 3> prev_pos{0, 0, 0};
    static std::array<double, 3> prev_vel{0, 0, 0};
    static bool have_prev = false;

    VAddr body = 0;
    if (!SafeRead(train + kTrainBody, body) || body == 0) {
        return false;
    }

    double px = 0, py = 0, pz = 0;
    if (!SafeRead(body + kBodyPos, px) || !SafeRead(body + kBodyPos + 8, py) ||
        !SafeRead(body + kBodyPos + 16, pz)) {
        return false;
    }
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) {
        return false;
    }

    float m[9]{};
    if (!SafeRead(body + kBodyRot, m, sizeof(m))) {
        return false;
    }
    for (float v : m) {
        if (!std::isfinite(v)) {
            return false;
        }
    }

    Mat3ToQuat(m, out.quat_x, out.quat_y, out.quat_z, out.quat_w);
    out.pos_x = static_cast<float>(px);
    out.pos_y = static_cast<float>(py);
    out.pos_z = static_cast<float>(pz);
    if (!SafeRead(train + kTrainSpeed, out.speed)) {
        return false;
    }
    s32 state = 2;
    SafeRead(train + kTrainState, state);
    const bool moving = state != 2;
    out.flags = (moving ? 1u : 0u) | 8u;
    if (state == 1) {
        out.flags |= 2u;
    }

    if (have_prev && dt > 1e-4 && dt < 0.25) {
        const double vx = (px - prev_pos[0]) / dt;
        const double vy = (py - prev_pos[1]) / dt;
        const double vz = (pz - prev_pos[2]) / dt;
        const double ax = (vx - prev_vel[0]) / dt;
        const double ay = (vy - prev_vel[1]) / dt;
        const double az = (vz - prev_vel[2]) / dt;
        float lx, ly, lz;
        WorldToLocal(m, static_cast<float>(ax), static_cast<float>(ay + 9.81),
                     static_cast<float>(az), lx, ly, lz);
        out.gforce_x = lx / 9.81f;
        out.gforce_y = ly / 9.81f;
        out.gforce_z = lz / 9.81f;
        prev_vel = {vx, vy, vz};
    } else {
        out.gforce_x = 0.f;
        out.gforce_y = 1.f;
        out.gforce_z = 0.f;
        prev_vel = {0, 0, 0};
    }
    prev_pos = {px, py, pz};
    have_prev = true;
    return true;
}

void SendUdp(const Sample& sample) {
    static int sock = -1;
#ifdef _WIN32
    static bool wsa_ok = false;
    if (!wsa_ok) {
        WSADATA data{};
        wsa_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!wsa_ok) {
            return;
        }
    }
#endif
    if (sock < 0) {
        sock = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (sock < 0) {
            return;
        }
    }

    alignas(4) std::array<u8, kPacketSize> packet{};
    auto write_f = [&](size_t off, float v) { std::memcpy(packet.data() + off, &v, 4); };
    auto write_u = [&](size_t off, u32 v) { std::memcpy(packet.data() + off, &v, 4); };
    write_f(0, sample.speed);
    write_f(4, sample.pos_x);
    write_f(8, sample.pos_y);
    write_f(12, sample.pos_z);
    write_f(16, sample.quat_x);
    write_f(20, sample.quat_y);
    write_f(24, sample.quat_z);
    write_f(28, sample.quat_w);
    write_f(32, sample.gforce_x);
    write_f(36, sample.gforce_y);
    write_f(40, sample.gforce_z);
    write_u(44, sample.flags);
    write_u(48, sample.frame);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kFeedPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    sendto(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

void OnFrameImpl(VAddr eboot_base) {
    static std::atomic<u32> frame{0};
    static auto last = std::chrono::steady_clock::now();
    static bool logged_ready = false;
    static VAddr cached_train = 0;

    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;

    Sample sample{};
    sample.frame = frame.fetch_add(1, std::memory_order_relaxed);
    sample.flags = 8u;
    sample.quat_w = 1.f;
    sample.gforce_y = 1.f;

    // Re-resolve periodically; pointers move across level loads.
    if ((sample.frame % 30) == 0 || cached_train == 0 || !LooksLikeTrain(cached_train)) {
        cached_train = FindPlayerTrain(eboot_base);
    }

    if (cached_train && SampleTrain(cached_train, sample, dt)) {
        if (!logged_ready) {
            LOG_INFO(Core, "BloodRush motion telemetry: cart feed -> 127.0.0.1:{}", kFeedPort);
            logged_ready = true;
        }
    }

    SendUdp(sample);
}

} // namespace

void OnFrame(VAddr eboot_base) {
    if (!Enabled() || eboot_base == 0) {
        return;
    }
#ifdef _WIN32
    __try {
        OnFrameImpl(eboot_base);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let a bad guest pointer take down the process.
    }
#else
    OnFrameImpl(eboot_base);
#endif
}

} // namespace Core::RobMotionTelemetry
