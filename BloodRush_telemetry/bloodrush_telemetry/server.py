"""BloodRush TCP telemetry server (request/reply)."""

from __future__ import annotations

import logging
import socket
import socketserver
import threading
from dataclasses import replace
from typing import Callable, Optional

from . import protocol as proto
from .sample import SampleStore

log = logging.getLogger("bloodrush_telemetry.server")


class _Handler(socketserver.BaseRequestHandler):
    store: SampleStore
    stale_seconds: float

    def handle(self) -> None:
        peer = self.client_address
        log.info("client connected %s:%s", peer[0], peer[1])
        buf = bytearray()
        self.request.settimeout(30.0)
        try:
            while True:
                try:
                    chunk = self.request.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                buf.extend(chunk)
                while True:
                    msg, consumed = proto.try_parse(buf)
                    if consumed == 0:
                        break
                    del buf[:consumed]
                    if msg is None:
                        continue
                    reply = self._dispatch(msg)
                    if reply:
                        self.request.sendall(reply)
        except ConnectionResetError:
            pass
        finally:
            log.info("client disconnected %s:%s", peer[0], peer[1])

    def _dispatch(self, msg: proto.Message) -> Optional[bytes]:
        if msg.type_id == proto.MSG_IDLE:
            return proto.reply_ok(msg.request_id)
        if msg.type_id == proto.MSG_GET_VERSION:
            return proto.reply_version(msg.request_id)
        if msg.type_id == proto.MSG_GET_TELEMETRY:
            sample = self.store.get()
            if self.stale_seconds > 0 and self.store.age() > self.stale_seconds:
                sample = replace(sample, in_play=False, paused=True)
            return proto.reply_telemetry(msg.request_id, sample.pack_telemetry())
        log.debug("unhandled message type %s (ok stub)", msg.type_id)
        return proto.reply_ok(msg.request_id)


class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


def serve(
    store: SampleStore,
    host: str = "127.0.0.1",
    port: int = 15151,
    *,
    stale_seconds: float = 1.0,
    ready: Optional[Callable[[], None]] = None,
) -> ThreadedTCPServer:
    handler = type(
        "BoundHandler",
        (_Handler,),
        {"store": store, "stale_seconds": stale_seconds},
    )
    server = ThreadedTCPServer((host, port), handler)
    log.info("BloodRush telemetry listening on %s:%s", host, port)
    if ready:
        ready()
    thread = threading.Thread(target=server.serve_forever, name="br-telemetry-tcp", daemon=True)
    thread.start()
    return server
