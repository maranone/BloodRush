"""CLI: python -m bloodrush_telemetry [--demo]"""

from __future__ import annotations

import argparse
import logging
import math
import signal
import sys
import time


def _euler_deg(qx: float, qy: float, qz: float, qw: float) -> tuple[float, float, float]:
    """Quaternion → pitch, roll, yaw degrees (Y-up)."""
    sinr = 2.0 * (qw * qx + qy * qz)
    cosr = 1.0 - 2.0 * (qx * qx + qy * qy)
    roll = math.degrees(math.atan2(sinr, cosr))
    sinp = 2.0 * (qw * qy - qz * qx)
    sinp = max(-1.0, min(1.0, sinp))
    pitch = math.degrees(math.asin(sinp))
    siny = 2.0 * (qw * qz + qx * qy)
    cosy = 1.0 - 2.0 * (qy * qy + qz * qz)
    yaw = math.degrees(math.atan2(siny, cosy))
    return pitch, roll, yaw


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="bloodrush_telemetry", description="BloodRush motion telemetry bridge")
    parser.add_argument("--host", default="127.0.0.1", help="TCP bind address (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=15151, help="TCP telemetry port (default 15151)")
    parser.add_argument("--feed-host", default="127.0.0.1", help="UDP feed bind address")
    parser.add_argument("--feed-port", type=int, default=25151, help="UDP feed port (default 25151)")
    parser.add_argument("--demo", action="store_true", help="synthesize a looping ride (no game needed)")
    parser.add_argument("--no-feed", action="store_true", help="do not open the UDP feed socket")
    parser.add_argument("--stale", type=float, default=1.0, help="seconds before feed is treated as paused")
    parser.add_argument("--quiet", action="store_true", help="do not print the live cart line")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    from .demo import start_demo
    from .feed_udp import start_udp_feed
    from .sample import SampleStore
    from .server import serve

    store = SampleStore()
    if args.demo:
        start_demo(store)
    elif not args.no_feed:
        start_udp_feed(store, args.feed_host, args.feed_port)

    server = serve(store, args.host, args.port, stale_seconds=args.stale)

    stop = False

    def _stop(*_args: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, _stop)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, _stop)

    mode = "demo" if args.demo else ("idle" if args.no_feed else f"udp-feed :{args.feed_port}")
    logging.getLogger("bloodrush_telemetry").info(
        "BloodRush_telemetry ready (mode=%s, tcp=%s:%s). Waiting for game UDP on %s:%s. Ctrl+C to stop.",
        mode,
        args.host,
        args.port,
        args.feed_host,
        args.feed_port,
    )

    try:
        while not stop:
            if not args.quiet:
                s = store.get()
                age = store.age()
                pitch, roll, yaw = _euler_deg(s.quat_x, s.quat_y, s.quat_z, s.quat_w)
                status = "PLAY" if s.in_play and age <= args.stale else "IDLE"
                line = (
                    f"\r{status} frame={s.frame:6d}  spd={s.speed:6.2f} m/s  "
                    f"pitch={pitch:7.2f}  roll={roll:7.2f}  yaw={yaw:7.2f}  "
                    f"G=({s.gforce_x:+5.2f},{s.gforce_y:+5.2f},{s.gforce_z:+5.2f})  "
                    f"age={age:4.2f}s   "
                )
                sys.stdout.write(line)
                sys.stdout.flush()
            time.sleep(0.1)
    finally:
        if not args.quiet:
            sys.stdout.write("\n")
        server.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
