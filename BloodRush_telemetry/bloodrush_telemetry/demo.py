"""Synthetic ride motion for testing motion software without the game."""

from __future__ import annotations

import logging
import math
import threading
import time

from .sample import CartSample, SampleStore

log = logging.getLogger("bloodrush_telemetry.demo")


def start_demo(store: SampleStore, hz: float = 90.0) -> threading.Thread:
    period = 1.0 / max(hz, 1.0)
    log.info("demo ride synthesizer at %.0f Hz", hz)

    def loop() -> None:
        t0 = time.monotonic()
        frame = 0
        while True:
            t = time.monotonic() - t0
            phase = (t % 20.0) / 20.0
            if phase < 0.35:
                u = phase / 0.35
                pitch = 25.0 * u
                roll = 0.0
                speed = 4.0 + 2.0 * u
                gx, gy, gz = 0.0, 1.0, -0.15
                braking = False
            elif phase < 0.50:
                u = (phase - 0.35) / 0.15
                pitch = 25.0 - 70.0 * u
                roll = 8.0 * math.sin(u * math.pi)
                speed = 6.0 + 30.0 * u
                gx, gy, gz = roll / 40.0, 0.4 + 1.2 * u, 1.5 * u
                braking = False
            elif phase < 0.80:
                u = (phase - 0.50) / 0.30
                pitch = -20.0 * math.sin(u * math.pi * 2)
                roll = 35.0 * math.sin(u * math.pi * 2)
                speed = 28.0 - 6.0 * abs(math.sin(u * math.pi))
                gx = math.sin(u * math.pi * 2) * 1.4
                gy = 1.0 + abs(math.sin(u * math.pi * 2)) * 0.8
                gz = -0.2 * math.cos(u * math.pi * 2)
                braking = False
            else:
                u = (phase - 0.80) / 0.20
                pitch = 0.0
                roll = 0.0
                speed = max(0.0, 22.0 * (1.0 - u))
                gx, gy, gz = 0.0, 1.0, -1.2 * (1.0 - u)
                braking = True

            yaw = (t * 18.0) % 360.0
            sample = CartSample.from_euler_deg(
                pitch,
                roll,
                yaw,
                speed=speed,
                gx=gx,
                gy=gy,
                gz=gz,
                in_play=True,
                frame=frame,
            )
            sample.braking = braking
            sample.pos_x = math.sin(math.radians(yaw)) * 40.0
            sample.pos_y = 10.0 + pitch * 0.2
            sample.pos_z = math.cos(math.radians(yaw)) * 40.0
            store.set(sample)
            frame += 1
            time.sleep(period)

    thread = threading.Thread(target=loop, name="demo-ride", daemon=True)
    thread.start()
    return thread
