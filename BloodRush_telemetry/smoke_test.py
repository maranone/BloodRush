"""Quick local check: start server briefly, ask for one telemetry sample, exit."""

from __future__ import annotations

import socket
import struct
import sys
import time

from bloodrush_telemetry.demo import start_demo
from bloodrush_telemetry.protocol import (
    MSG_GET_TELEMETRY,
    MSG_TELEMETRY,
    Message,
    try_parse,
)
from bloodrush_telemetry.sample import SampleStore
from bloodrush_telemetry.server import serve


def main() -> int:
    store = SampleStore()
    start_demo(store, hz=60.0)
    server = serve(store, "127.0.0.1", 15151, stale_seconds=5.0)
    time.sleep(0.15)

    req = Message(MSG_GET_TELEMETRY, request_id=1, data=b"").pack()
    with socket.create_connection(("127.0.0.1", 15151), timeout=2.0) as sock:
        sock.sendall(req)
        buf = bytearray()
        msg = None
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf.extend(chunk)
            msg, consumed = try_parse(buf)
            if msg is not None:
                break

    server.shutdown()
    server.server_close()

    if msg is None:
        print("FAIL: no reply")
        return 1
    if msg.type_id != MSG_TELEMETRY or len(msg.data) != 76:
        print(f"FAIL: bad reply type={msg.type_id} size={len(msg.data)}")
        return 1

    fields = struct.unpack("!iiiiiiiifffffffffff", msg.data)
    flags, frame = fields[0], fields[1]
    speed = fields[8]
    qx, qy, qz, qw = fields[12:16]
    gx, gy, gz = fields[16:19]
    print(
        "OK telemetry "
        f"flags=0x{flags:x} frame={frame} speed={speed:.2f} "
        f"quat=({qx:.3f},{qy:.3f},{qz:.3f},{qw:.3f}) "
        f"g=({gx:.2f},{gy:.2f},{gz:.2f})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
