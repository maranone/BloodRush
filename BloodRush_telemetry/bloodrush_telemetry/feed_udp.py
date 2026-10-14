"""UDP ingest for live cart samples from the emulator host.

Packet (little-endian, 52 bytes):

  float  speed
  float  pos_x, pos_y, pos_z
  float  quat_x, quat_y, quat_z, quat_w
  float  gforce_x, gforce_y, gforce_z
  uint32 flags   bit0=in_play bit1=braking bit2=paused bit3=vr
  uint32 frame

Default bind: 127.0.0.1:25151
"""

from __future__ import annotations

import logging
import socket
import struct
import threading

from .sample import CartSample, SampleStore

log = logging.getLogger("bloodrush_telemetry.feed")

# 11 floats + 2 uint32 = 52 bytes (C++ may send 52 or padded 56)
FEED_FORMAT = "<fffffffffffII"
FEED_SIZE = struct.calcsize(FEED_FORMAT)
assert FEED_SIZE == 52


def decode_feed(packet: bytes) -> CartSample:
    if len(packet) < FEED_SIZE:
        raise ValueError(f"feed packet too short: {len(packet)} < {FEED_SIZE}")
    (
        speed,
        px,
        py,
        pz,
        qx,
        qy,
        qz,
        qw,
        gx,
        gy,
        gz,
        flags,
        frame,
    ) = struct.unpack_from(FEED_FORMAT, packet)
    return CartSample(
        in_play=bool(flags & 1),
        braking=bool(flags & 2),
        paused=bool(flags & 4),
        vr_headset=bool(flags & 8),
        frame=frame,
        speed=speed,
        pos_x=px,
        pos_y=py,
        pos_z=pz,
        quat_x=qx,
        quat_y=qy,
        quat_z=qz,
        quat_w=qw,
        gforce_x=gx,
        gforce_y=gy,
        gforce_z=gz,
    )


def encode_feed(sample: CartSample) -> bytes:
    flags = 0
    if sample.in_play:
        flags |= 1
    if sample.braking:
        flags |= 2
    if sample.paused:
        flags |= 4
    if sample.vr_headset:
        flags |= 8
    return struct.pack(
        FEED_FORMAT,
        sample.speed,
        sample.pos_x,
        sample.pos_y,
        sample.pos_z,
        sample.quat_x,
        sample.quat_y,
        sample.quat_z,
        sample.quat_w,
        sample.gforce_x,
        sample.gforce_y,
        sample.gforce_z,
        flags,
        sample.frame & 0xFFFFFFFF,
    )


def start_udp_feed(
    store: SampleStore,
    host: str = "127.0.0.1",
    port: int = 25151,
) -> threading.Thread:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(1.0)
    log.info("UDP feed listening on %s:%s (%d-byte packets)", host, port, FEED_SIZE)

    def loop() -> None:
        while True:
            try:
                data, _addr = sock.recvfrom(256)
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                store.set(decode_feed(data))
            except ValueError as exc:
                log.warning("bad feed packet: %s", exc)

    thread = threading.Thread(target=loop, name="udp-feed", daemon=True)
    thread.start()
    return thread
