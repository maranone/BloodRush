"""BloodRush motion telemetry binary framing (TCP request/reply, big-endian)."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Optional

MAGIC_START = b"N"
MAGIC_END = b"L"

MSG_IDLE = 0
MSG_OK = 1
MSG_ERROR = 2
MSG_GET_VERSION = 3
MSG_VERSION = 4
MSG_GET_TELEMETRY = 5
MSG_TELEMETRY = 6

HEADER = struct.Struct("!cHIH")  # start, type, request_id, data_size


@dataclass
class Message:
    type_id: int
    request_id: int
    data: bytes

    def pack(self) -> bytes:
        return (
            HEADER.pack(MAGIC_START, self.type_id, self.request_id, len(self.data))
            + self.data
            + MAGIC_END
        )


def try_parse(buffer: bytearray) -> tuple[Optional[Message], int]:
    """Parse one message from the front of buffer. Returns (msg|None, bytes_consumed)."""
    if len(buffer) < 10:
        return None, 0
    if buffer[0:1] != MAGIC_START:
        try:
            nxt = buffer.index(MAGIC_START, 1)
            return None, nxt
        except ValueError:
            return None, len(buffer)

    _start, type_id, request_id, data_size = HEADER.unpack_from(buffer, 0)
    total = 10 + data_size
    if len(buffer) < total:
        return None, 0
    if buffer[total - 1 : total] != MAGIC_END:
        return None, 1

    data = bytes(buffer[9 : 9 + data_size])
    return Message(type_id, request_id, data), total


def reply_ok(request_id: int) -> bytes:
    return Message(MSG_OK, request_id, b"").pack()


def reply_version(
    request_id: int, major: int = 1, minor: int = 0, patch: int = 0, build: int = 0
) -> bytes:
    return Message(
        MSG_VERSION,
        request_id,
        bytes((major & 0xFF, minor & 0xFF, patch & 0xFF, build & 0xFF)),
    ).pack()


def reply_telemetry(request_id: int, payload_76: bytes) -> bytes:
    if len(payload_76) != 76:
        raise ValueError(f"telemetry payload must be 76 bytes, got {len(payload_76)}")
    return Message(MSG_TELEMETRY, request_id, payload_76).pack()


def reply_error(request_id: int, message: str) -> bytes:
    return Message(MSG_ERROR, request_id, message.encode("utf-8")).pack()
