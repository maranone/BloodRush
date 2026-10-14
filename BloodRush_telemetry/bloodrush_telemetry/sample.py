"""Latest cart sample shared between the feed and the TCP server."""

from __future__ import annotations

import math
import struct
import threading
import time
from dataclasses import dataclass, field


@dataclass
class CartSample:
    """Cart pose / forces for a 76-byte telemetry payload."""

    in_play: bool = False
    braking: bool = False
    paused: bool = False
    vr_headset: bool = True
    frame: int = 0
    view_mode: int = 1  # ride view
    coaster: int = 0
    coaster_style: int = 0
    train: int = 0
    car: int = 0
    seat: int = 0
    speed: float = 0.0  # m/s
    pos_x: float = 0.0
    pos_y: float = 0.0
    pos_z: float = 0.0
    quat_x: float = 0.0
    quat_y: float = 0.0
    quat_z: float = 0.0
    quat_w: float = 1.0
    gforce_x: float = 0.0  # lateral
    gforce_y: float = 1.0  # vertical (1g at rest)
    gforce_z: float = 0.0  # forward
    updated_at: float = field(default_factory=time.monotonic)

    def state_flags(self) -> int:
        flags = 0
        if self.in_play:
            flags |= 1 << 0
        if self.braking:
            flags |= 1 << 1
        if self.paused:
            flags |= 1 << 2
        if self.vr_headset:
            flags |= 1 << 3
        return flags

    def pack_telemetry(self) -> bytes:
        """76-byte telemetry payload, big-endian."""
        # 8 int32 + 11 float32 = 76 bytes
        return struct.pack(
            "!iiiiiiiifffffffffff",
            self.state_flags(),
            self.frame & 0x7FFFFFFF,
            self.view_mode,
            self.coaster,
            self.coaster_style,
            self.train,
            self.car,
            self.seat,
            self.speed,
            self.pos_x,
            self.pos_y,
            self.pos_z,
            self.quat_x,
            self.quat_y,
            self.quat_z,
            self.quat_w,
            self.gforce_x,
            self.gforce_y,
            self.gforce_z,
        )

    @staticmethod
    def from_euler_deg(
        pitch_deg: float,
        roll_deg: float,
        yaw_deg: float = 0.0,
        *,
        speed: float = 0.0,
        gx: float = 0.0,
        gy: float = 1.0,
        gz: float = 0.0,
        in_play: bool = True,
        frame: int = 0,
    ) -> "CartSample":
        """Build a sample from pitch/roll/yaw degrees (Y-up)."""
        cy = math.cos(math.radians(yaw_deg) * 0.5)
        sy = math.sin(math.radians(yaw_deg) * 0.5)
        cp = math.cos(math.radians(pitch_deg) * 0.5)
        sp = math.sin(math.radians(pitch_deg) * 0.5)
        cr = math.cos(math.radians(roll_deg) * 0.5)
        sr = math.sin(math.radians(roll_deg) * 0.5)
        return CartSample(
            in_play=in_play,
            frame=frame,
            speed=speed,
            quat_x=sr * cp * cy - cr * sp * sy,
            quat_y=cr * sp * cy + sr * cp * sy,
            quat_z=cr * cp * sy - sr * sp * cy,
            quat_w=cr * cp * cy + sr * sp * sy,
            gforce_x=gx,
            gforce_y=gy,
            gforce_z=gz,
        )


class SampleStore:
    """Thread-safe latest-sample holder."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._sample = CartSample()

    def set(self, sample: CartSample) -> None:
        sample.updated_at = time.monotonic()
        with self._lock:
            self._sample = sample

    def get(self) -> CartSample:
        with self._lock:
            return self._sample

    def age(self) -> float:
        with self._lock:
            return time.monotonic() - self._sample.updated_at
