// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SDL3/SDL_events.h>

#include "common/logging/log.h"
#include "common/singleton.h"
#include "common/thread.h"
#include "core/libraries/move/move.h"
#include "core/libraries/pad/pad.h"
#include "core/vr/vr_runtime.h"
#include "input/controller.h"
#include "input/desktop_play.h"

namespace Input::DesktopPlay {

namespace {

using namespace Core::Vr;
using namespace Libraries::Move;

constexpr float Degrees = 3.14159265f / 180.0f;

// Which of the player's keys and buttons are down, one bit each.
enum Key : u32 {
    LeanLeft = 1 << 0,
    LeanRight = 1 << 1,
    Crouch = 1 << 2,
    Fire = 1 << 3,
    Reload = 1 << 4,
    Flick = 1 << 5,
    Cross = 1 << 6,
    Circle = 1 << 7,
    Triangle = 1 << 8,
    Square = 1 << 9,
    Start = 1 << 10,
    Recenter = 1 << 11,
    PadCross = 1 << 12,
    TiltUp = 1 << 13,
    TiltDown = 1 << 14,
    TiltReset = 1 << 15,
    TurnLeft = 1 << 16,
    TurnRight = 1 << 17,
    Tilting = TiltUp | TiltDown | TurnLeft | TurnRight,
};

std::atomic<u32> keys{0};
std::atomic<float> mouse_x{0.0f};
std::atomic<float> mouse_y{0.0f};
// Degrees the Move guns are turned up and to the right on their own, for a title whose own
// calibration leaves them pointing off: SHADPS4_DESKTOP_GUN_PITCH / _GUN_YAW, then W/S and A/D
// held (Home: none).
float EnvDegrees(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? static_cast<float>(std::atof(value)) : 0.0f;
}
float gun_pitch = EnvDegrees("SHADPS4_DESKTOP_GUN_PITCH");
float gun_yaw = EnvDegrees("SHADPS4_DESKTOP_GUN_YAW");

u32 KeyFor(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_Q:
        return LeanLeft;
    case SDL_SCANCODE_E:
        return LeanRight;
    case SDL_SCANCODE_C:
    case SDL_SCANCODE_LCTRL:
        return Crouch;
    case SDL_SCANCODE_SPACE:
        return Cross;
    case SDL_SCANCODE_BACKSPACE:
        return Circle;
    case SDL_SCANCODE_T:
        return Triangle;
    case SDL_SCANCODE_X:
        return Square;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_ESCAPE:
        return Start;
    case SDL_SCANCODE_R:
        return Recenter;
    case SDL_SCANCODE_W:
        return TiltUp;
    case SDL_SCANCODE_S:
        return TiltDown;
    case SDL_SCANCODE_A:
        return TurnLeft;
    case SDL_SCANCODE_D:
        return TurnRight;
    case SDL_SCANCODE_HOME:
        return TiltReset;
    case SDL_SCANCODE_Z:
        return PadCross;
    default:
        return 0;
    }
}

u32 ButtonFor(u8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return Fire;
    case SDL_BUTTON_RIGHT:
        // A gamepad reloads with a flick; Move controllers with their Move button.
        return std::getenv("SHADPS4_DESKTOP_DEVICE") != nullptr &&
                       std::string_view{std::getenv("SHADPS4_DESKTOP_DEVICE")} == "pad"
                   ? Flick
                   : Reload;
    case SDL_BUTTON_MIDDLE:
        return Flick;
    default:
        return 0;
    }
}

void Set(u32 key, bool down) {
    if (down) {
        keys.fetch_or(key, std::memory_order_relaxed);
    } else {
        keys.fetch_and(~key, std::memory_order_relaxed);
    }
}

void Add(std::atomic<float>& value, float by) {
    float old = value.load(std::memory_order_relaxed);
    while (!value.compare_exchange_weak(old, old + by, std::memory_order_relaxed)) {
    }
}

Vec3 Plus(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

// Points the controller's -Z (where a Move points) from `from` at `at`.
Quat Aim(const Vec3& from, const Vec3& at) {
    Vec3 d{at.x - from.x, at.y - from.y, at.z - from.z};
    const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (length < 1e-4f) {
        return {};
    }
    d = {d.x / length, d.y / length, d.z / length};
    return FromYawPitch(std::atan2(-d.x, -d.z), std::asin(std::clamp(d.y, -1.0f, 1.0f)));
}

// SHADPS4_DESKTOP_TEST=<seconds>:<what>[,...], for unattended runs: at so many seconds after
// start, "fire" or "reload" is clicked for 0.2 s, or "x<counts>" / "y<counts>" moves the mouse.
void Test() {
    const char* at = std::getenv("SHADPS4_DESKTOP_TEST");
    if (at == nullptr) {
        return;
    }
    struct Step {
        float time;
        std::string what;
    };
    std::vector<Step> steps;
    while (*at != '\0') {
        const char* end = std::strchr(at, ',');
        const std::string item = end ? std::string(at, end) : std::string(at);
        if (const auto colon = item.find(':'); colon != std::string::npos) {
            steps.push_back({static_cast<float>(std::atof(item.c_str())), item.substr(colon + 1)});
        }
        at = end ? end + 1 : at + std::strlen(at);
    }
    std::thread{[steps] {
        const auto start = std::chrono::steady_clock::now();
        for (const Step& step : steps) {
            std::this_thread::sleep_until(start + std::chrono::duration<float>(step.time));
            LOG_INFO(Input, "Desktop play test at {:.1f} s: {}", step.time, step.what);
            if (step.what == "fire" || step.what == "reload") {
                const u32 key = step.what == "fire" ? Fire : Reload;
                Set(key, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                Set(key, false);
            } else if (step.what[0] == 'x') {
                Add(mouse_x, static_cast<float>(std::atof(step.what.c_str() + 1)));
            } else if (step.what[0] == 'y') {
                Add(mouse_y, static_cast<float>(std::atof(step.what.c_str() + 1)));
            }
        }
    }}.detach();
}

void Feed() {
    Common::SetCurrentThreadName("shadPS4:DesktopPlay");
    Test();
    static const float sensitivity = [] {
        const char* value = std::getenv("SHADPS4_DESKTOP_SENS");
        const float degrees = value != nullptr ? static_cast<float>(std::atof(value)) : 0.08f;
        return std::clamp(degrees, 0.005f, 2.0f) * Degrees;
    }();
    // SHADPS4_DESKTOP_DEVICE=pad: the player holds the gamepad (Rush of Blood's "Wireless
    // Controller") instead of a pair of Move controllers.
    static const bool use_pad = [] {
        const char* value = std::getenv("SHADPS4_DESKTOP_DEVICE");
        return value != nullptr && std::string_view{value} == "pad";
    }();
    auto* const controller = (*Common::Singleton<GameControllers>::Instance())[0];
    Libraries::Pad::OrbisPadButtonDataOffset previous_pad{};
    auto& vr = Runtime::Instance();
    // Poses are counted from where the player sits, as for scripted input.
    vr.FixSeat();

    float yaw = 0.0f;
    float pitch = 0.0f;
    Vec3 lean{};
    float roll = 0.0f;
    u32 previous{};
    auto last = std::chrono::steady_clock::now();
    auto flick_until = last;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.05f);
        last = now;
        const u32 down = keys.load(std::memory_order_relaxed);
        const u32 pressed = down & ~previous;
        previous = down;

        // (Turning right and looking down are negative angles.)
        yaw = std::clamp(yaw - mouse_x.exchange(0.0f, std::memory_order_relaxed) * sensitivity,
                         -150.0f * Degrees, 150.0f * Degrees);
        pitch = std::clamp(pitch - mouse_y.exchange(0.0f, std::memory_order_relaxed) * sensitivity,
                           -80.0f * Degrees, 80.0f * Degrees);
        if (pressed & Recenter) {
            yaw = pitch = 0.0f;
        }

        // Leaning and crouching ease in and out, as a body does.
        const bool both = (down & LeanLeft) && (down & LeanRight);
        const float side = both ? 0.0f
                                : (down & LeanLeft) ? -1.0f
                                : (down & LeanRight) ? 1.0f
                                                     : 0.0f;
        const bool crouch = both || (down & Crouch);
        const Quat body = FromYawPitch(yaw, 0.0f);
        const Vec3 lean_target = Rotate(body, {side * 0.32f, crouch ? -0.4f : 0.0f, 0.0f});
        const float ease = 1.0f - std::exp(-dt / 0.08f);
        lean.x += (lean_target.x - lean.x) * ease;
        lean.y += (lean_target.y - lean.y) * ease;
        lean.z += (lean_target.z - lean.z) * ease;
        roll += (-side * 12.0f * Degrees - roll) * ease;

        const Quat head_turn = FromYawPitchRoll(yaw, pitch, roll);
        vr.UpdateHead({.pose = {.position = lean, .orientation = head_turn}, .tracked = true});
        if (pressed & Recenter) {
            vr.RequestRecenter();
        }

        // Both guns point at what is in the middle of the view, some way off.
        const Vec3 gaze = Rotate(FromYawPitch(yaw, pitch), {0.0f, 0.0f, -1.0f});
        const Vec3 target = Plus(lean, {gaze.x * 8.0f, gaze.y * 8.0f, gaze.z * 8.0f});
        if (pressed & Flick) {
            flick_until = now + std::chrono::milliseconds(use_pad ? 200 : 150);
        }
        const bool flicking = now < flick_until;
        u16 buttons = 0;
        buttons |= (down & Fire) ? ORBIS_MOVE_BUTTON_T : 0;
        buttons |= (down & Reload) ? ORBIS_MOVE_BUTTON_MOVE : 0;
        buttons |= (down & Cross) ? ORBIS_MOVE_BUTTON_CROSS : 0;
        buttons |= (down & Circle) ? ORBIS_MOVE_BUTTON_CIRCLE : 0;
        buttons |= (down & Triangle) ? ORBIS_MOVE_BUTTON_TRIANGLE : 0;
        buttons |= (down & Square) ? ORBIS_MOVE_BUTTON_SQUARE : 0;
        if (use_pad) {
            // Held before the chest, pointing at what is in the middle of the view. Its flick
            // (how a gamepad reloads in Rush of Blood) is a real one, 0.2 s long: the front
            // tips up 40 degrees and the controller rises 12 cm, and both come back. Its
            // sensors and the camera see that as it happens. (SHADPS4_DESKTOP_FLICK=down for
            // the other way.)
            using Libraries::Pad::OrbisPadButtonDataOffset;
            static const float way = [] {
                const char* value = std::getenv("SHADPS4_DESKTOP_FLICK");
                return value != nullptr && std::string_view{value} == "down" ? -1.0f : 1.0f;
            }();
            Vec3 at = Plus(lean, Rotate(body, {0.0f, -0.3f, -0.35f}));
            Quat aim = Aim(at, target);
            float tilt_rate = 0.0f;
            Vec3 velocity{};
            float lift_acceleration = 0.0f;
            if (flicking) {
                constexpr float Length = 0.2f;
                constexpr float W = 2.0f * 3.14159265f / Length;
                const float t =
                    Length - std::chrono::duration<float>(flick_until - now).count();
                const float tilt = way * 20.0f * Degrees * (1.0f - std::cos(W * t));
                tilt_rate = way * 20.0f * Degrees * W * std::sin(W * t);
                const float lift = way * 0.06f * (1.0f - std::cos(W * t));
                velocity = {0.0f, way * 0.06f * W * std::sin(W * t), 0.0f};
                lift_acceleration = way * 0.06f * W * W * std::cos(W * t);
                aim = Normalize(Multiply(aim, FromYawPitch(0.0f, tilt)));
                at.y += lift;
            }
            vr.UpdatePad({.pose = {.position = at, .orientation = aim},
                          .linear_velocity = velocity,
                          .angular_velocity = Rotate(aim, {tilt_rate, 0.0f, 0.0f}),
                          .tracked = true});
            // The motion sensors, in the controller's frame (+Y out of its face): turning about
            // its own X, and gravity plus the lift.
            const float gyro[3]{tilt_rate, 0.0f, 0.0f};
            const Vec3 felt = Rotate(Conjugate(aim), {0.0f, 9.80665f + lift_acceleration, 0.0f});
            const float acceleration[3]{felt.x, felt.y, felt.z};
            controller->UpdateGyro(gyro);
            controller->UpdateAcceleration(acceleration);
            OrbisPadButtonDataOffset pad{};
            if (down & Fire) {
                // Both guns, and Cross for the menus.
                pad |= OrbisPadButtonDataOffset::R2 | OrbisPadButtonDataOffset::L2 |
                       OrbisPadButtonDataOffset::Cross;
            }
            if (down & (Cross | PadCross)) {
                pad |= OrbisPadButtonDataOffset::Cross;
            }
            if (down & Circle) {
                pad |= OrbisPadButtonDataOffset::Circle;
            }
            if (down & Triangle) {
                pad |= OrbisPadButtonDataOffset::Triangle;
            }
            if (down & Square) {
                pad |= OrbisPadButtonDataOffset::Square;
            }
            if (down & Start) {
                pad |= OrbisPadButtonDataOffset::Options;
            }
            if (pad != previous_pad) {
                const int trigger = (down & Fire) ? 255 : 0;
                controller->ApplyRemoteState(pad, {128, 128, 128, 128, trigger, trigger}, false,
                                             0.0f, 0.0f);
                previous_pad = pad;
            }
            continue;
        }
        // 30 degrees a second while held; where they ended up is logged when let go.
        const float step = 30.0f * dt;
        gun_pitch += (down & TiltUp) ? step : (down & TiltDown) ? -step : 0.0f;
        gun_yaw += (down & TurnRight) ? step : (down & TurnLeft) ? -step : 0.0f;
        if (pressed & TiltReset) {
            gun_pitch = gun_yaw = 0.0f;
        }
        static u32 was_tilting = 0;
        if ((was_tilting & ~down & Tilting) || (pressed & TiltReset)) {
            LOG_INFO(Input, "Desktop play: guns turned {:.0f} degrees up, {:.0f} right",
                     gun_pitch, gun_yaw);
        }
        was_tilting = down & Tilting;
        for (int index = 0; index < MaxMoves; ++index) {
            // 0 is the right hand. The arms are held out as the game's calibration board shows
            // (straight ahead at shoulder height), so pressing Move there calibrates them right.
            const float hand = index == 0 ? 0.18f : -0.18f;
            const Vec3 at = Plus(lean, Rotate(body, {hand, -0.2f, -0.55f}));
            Quat aim = Normalize(
                Multiply(Aim(at, target), FromYawPitch(-gun_yaw * Degrees, gun_pitch * Degrees)));
            DeviceState state{.pose = {.position = at, .orientation = aim}, .tracked = true};
            if (flicking) {
                // A sharp turn downwards and back, the way a gun is shaken.
                state.pose.orientation = Normalize(
                    Multiply(aim, FromYawPitch(0.0f, -35.0f * Degrees)));
                state.angular_velocity = Rotate(body, {-8.0f, 0.0f, 0.0f});
            }
            const u16 own = index == 0 && (down & Start) ? ORBIS_MOVE_BUTTON_START : 0;
            vr.UpdateMove(index, state, buttons | own, (down & Fire) ? 255 : 0);
        }

        static bool pad_cross = false;
        if (const bool want = (down & PadCross) != 0; want != pad_cross) {
            pad_cross = want;
            controller->Button(Libraries::Pad::OrbisPadButtonDataOffset::Cross, want);
        }
    }
}

} // namespace

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SHADPS4_DESKTOP_PLAY");
        return value != nullptr && std::string_view{value} == "1";
    }();
    return enabled;
}

void Start() {
    LOG_INFO(Input, "Desktop play: mouse looks and aims, Q/E lean, Q+E or C crouch, left click "
                    "fires, right click is the Move button, Space Cross, Enter/Esc START, R recenters");
    std::thread{Feed}.detach();
}

bool OnEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        Add(mouse_x, event.motion.xrel);
        Add(mouse_y, event.motion.yrel);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        Set(ButtonFor(event.button.button), event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.key.scancode >= SDL_SCANCODE_F1 && event.key.scancode <= SDL_SCANCODE_F12) {
            return false;
        }
        Set(KeyFor(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN);
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        keys.store(0, std::memory_order_relaxed);
        return false;
    default:
        return false;
    }
}

} // namespace Input::DesktopPlay
