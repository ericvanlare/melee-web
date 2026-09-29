# SPDX-License-Identifier: MIT
"""Loopback-only WebSocket adapter for the local Slippi ENet relay experiment."""

from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import signal
import socket
import struct
import time
from typing import Any


HERE = Path(__file__).resolve().parent
WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PAD_HEX = re.compile(r"^[0-9a-fA-F]{16}$")
MAX_HEADER_BYTES = 8192
MAX_WEBSOCKET_MESSAGE_BYTES = 4096
MAX_BATCH_FRAMES = 64
MAX_SESSION_FRAMES = 128
MAX_EVENT_LOG_BYTES = 64 * 1024
CLIENT_EVENTS = {"pad_applied", "peer_pad"}


def _json_line(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def _loopback_peer(writer: asyncio.StreamWriter) -> bool:
    peer = writer.get_extra_info("peername")
    return bool(peer and peer[0] in ("127.0.0.1", "::1"))


async def _read_http_headers(reader: asyncio.StreamReader) -> list[str]:
    raw = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=5)
    if len(raw) > MAX_HEADER_BYTES:
        raise ValueError("HTTP header limit")
    try:
        return raw.decode("ascii").split("\r\n")[:-2]
    except UnicodeDecodeError as error:
        raise ValueError("non-ASCII HTTP header") from error


async def _read_frame(reader: asyncio.StreamReader) -> tuple[int, bytes]:
    header = await asyncio.wait_for(reader.readexactly(2), timeout=30)
    first, second = header
    if first & 0x70 or not first & 0x80 or not second & 0x80:
        raise ValueError("unsupported WebSocket frame flags")
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = struct.unpack(">H", await reader.readexactly(2))[0]
        if length < 126:
            raise ValueError("non-canonical WebSocket length")
    elif length == 127:
        length = struct.unpack(">Q", await reader.readexactly(8))[0]
        if length <= 0xFFFF:
            raise ValueError("non-canonical WebSocket length")
    if length > MAX_WEBSOCKET_MESSAGE_BYTES:
        raise ValueError("WebSocket message limit")
    if opcode >= 0x8 and length > 125:
        raise ValueError("WebSocket control frame limit")
    mask = await reader.readexactly(4)
    payload = bytearray(await reader.readexactly(length))
    for index in range(length):
        payload[index] ^= mask[index & 3]
    return opcode, bytes(payload)


def _server_frame(payload: bytes, opcode: int = 0x1) -> bytes:
    if len(payload) > MAX_WEBSOCKET_MESSAGE_BYTES:
        raise ValueError("server WebSocket message limit")
    prefix = bytes((0x80 | opcode,))
    if len(payload) < 126:
        return prefix + bytes((len(payload),)) + payload
    return prefix + bytes((126,)) + struct.pack(">H", len(payload)) + payload


class BrowserTransportServer:
    def __init__(self, input_log: Path, relay_event_log: Path, service_event_log: Path,
                 *, host: str = "127.0.0.1", port: int = 43116):
        if host != "127.0.0.1" or not 0 <= port <= 65535:
            raise ValueError("browser transport must bind to IPv4 loopback")
        self.host = host
        self.port = port
        self.input_log = Path(input_log)
        self.relay_event_log = Path(relay_event_log)
        self.service_event_log = Path(service_event_log)
        self._input_fd = -1
        self._event_fd = -1
        self._server: asyncio.AbstractServer | None = None
        self._active_session: str | None = None
        self._active_writer: asyncio.StreamWriter | None = None
        self._stopping = asyncio.Event()

    @property
    def origin(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    async def start(self) -> None:
        self._prepare_log(self.input_log)
        self._prepare_log(self.relay_event_log)
        self._prepare_log(self.service_event_log)
        self._server = await asyncio.start_server(
            self._handle_client, self.host, self.port, limit=MAX_HEADER_BYTES
        )
        self.port = self._server.sockets[0].getsockname()[1]
        self._event({"event": "browser_transport_started", "origin": self.origin})

    @staticmethod
    def _prepare_log(path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        descriptor = os.open(path, flags, 0o600)
        os.fchmod(descriptor, 0o600)
        os.close(descriptor)

    def _append_input(self, session_id: str, command: str, frame: int | None = None,
                      pad_hex: str | None = None) -> None:
        if self._input_fd < 0:
            self._input_fd = os.open(self.input_log, os.O_WRONLY | os.O_APPEND)
        if command == "OPEN":
            line = f"OPEN {session_id}\n"
        elif command == "CLOSE":
            line = f"CLOSE {session_id}\n"
        else:
            assert frame is not None and pad_hex is not None
            line = f"PAD {session_id} {frame} {pad_hex.lower()}\n"
        data = line.encode("ascii")
        current_size = os.fstat(self._input_fd).st_size
        if current_size + len(data) > MAX_EVENT_LOG_BYTES:
            raise ValueError("browser input log limit")
        written = os.write(self._input_fd, data)
        if written != len(data):
            raise OSError("short browser input log write")

    def _event(self, value: dict[str, Any]) -> None:
        if self._event_fd < 0:
            self._event_fd = os.open(self.service_event_log, os.O_WRONLY | os.O_APPEND)
        line = _json_line({"host_monotonic_ns": time.monotonic_ns(), **value})
        if os.fstat(self._event_fd).st_size + len(line) > MAX_EVENT_LOG_BYTES:
            return
        os.write(self._event_fd, line)

    async def _handle_client(self, reader: asyncio.StreamReader,
                             writer: asyncio.StreamWriter) -> None:
        try:
            if not _loopback_peer(writer):
                await self._http_reply(writer, 403, b"loopback only")
                return
            lines = await _read_http_headers(reader)
            if not lines:
                await self._http_reply(writer, 400, b"bad request")
                return
            parts = lines[0].split(" ")
            if len(parts) != 3 or parts[0] != "GET" or parts[2] != "HTTP/1.1":
                await self._http_reply(writer, 400, b"bad request")
                return
            headers: dict[str, str] = {}
            for line in lines[1:]:
                if not line:
                    continue
                name, separator, value = line.partition(":")
                key = name.strip().lower()
                if not separator or not key or key in headers:
                    await self._http_reply(writer, 400, b"bad headers")
                    return
                headers[key] = value.strip()
            if headers.get("host") != f"127.0.0.1:{self.port}":
                await self._http_reply(writer, 403, b"unexpected host")
                return
            if parts[1] == "/" and "upgrade" not in headers:
                await self._serve_page(writer)
                return
            if parts[1] != "/transport":
                await self._http_reply(writer, 404, b"not found")
                return
            await self._upgrade(reader, writer, headers)
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, TimeoutError,
                UnicodeError, ValueError, OSError) as error:
            self._event({"event": "browser_transport_connection_rejected",
                         "reason": type(error).__name__})
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except OSError:
                pass

    async def _upgrade(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                       headers: dict[str, str]) -> None:
        if headers.get("origin") != self.origin:
            await self._http_reply(writer, 403, b"origin rejected")
            self._event({"event": "browser_transport_origin_rejected"})
            return
        if (headers.get("upgrade", "").lower() != "websocket"
                or "upgrade" not in {value.strip() for value in
                                     headers.get("connection", "").lower().split(",")}
                or headers.get("sec-websocket-version") != "13"):
            await self._http_reply(writer, 400, b"unsupported WebSocket request")
            return
        key = headers.get("sec-websocket-key", "")
        try:
            decoded_key = base64.b64decode(key, validate=True)
        except (ValueError, base64.binascii.Error):
            decoded_key = b""
        if len(decoded_key) != 16:
            await self._http_reply(writer, 400, b"invalid WebSocket key")
            return
        if self._active_session is not None:
            await self._http_reply(writer, 409, b"one browser session is allowed")
            return

        accept = base64.b64encode(hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest())
        writer.write(
            b"HTTP/1.1 101 Switching Protocols\r\n"
            b"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
            + accept + b"\r\n\r\n"
        )
        await writer.drain()
        session_id = secrets.token_hex(16)
        self._active_session = session_id
        self._active_writer = writer
        self._append_input(session_id, "OPEN")
        self._event({"event": "browser_transport_connected", "session_id": session_id})
        await self._send_json(writer, {"event": "session_ready", "sessionId": session_id,
                                       "maxBatchFrames": MAX_BATCH_FRAMES,
                                       "playerPort": 1})
        pump = asyncio.create_task(self._pump_relay_events(reader, writer, session_id))
        try:
            last_frame = 0
            frame_total = 0
            while True:
                opcode, payload = await _read_frame(reader)
                if opcode == 0x8:
                    writer.write(_server_frame(payload[:125], 0x8))
                    await writer.drain()
                    break
                if opcode == 0x9:
                    writer.write(_server_frame(payload, 0xA))
                    await writer.drain()
                    continue
                if opcode == 0xA:
                    continue
                if opcode != 0x1:
                    await self._close_policy(writer, "text messages required")
                    break
                try:
                    message = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    await self._close_policy(writer, "invalid JSON")
                    break
                result = self._accept_pad_batch(message, session_id, last_frame, frame_total)
                if isinstance(result, str):
                    self._event({"event": "browser_transport_message_rejected",
                                 "session_id": session_id, "reason": result})
                    await self._close_policy(writer, result)
                    break
                last_frame, frame_total, frames = result
                self._event({"event": "browser_pad_batch_accepted", "session_id": session_id,
                             "frame_count": len(frames), "first_frame": frames[0][0],
                             "last_frame": frames[-1][0]})
                await self._send_json(writer, {"event": "pad_queued", "sessionId": session_id,
                                               "count": len(frames),
                                               "firstFrame": frames[0][0],
                                               "lastFrame": frames[-1][0]})
                for frame, pad_hex in frames:
                    self._append_input(session_id, "PAD", frame, pad_hex)
        finally:
            pump.cancel()
            try:
                await pump
            except asyncio.CancelledError:
                pass
            if session_id == self._active_session:
                try:
                    self._append_input(session_id, "CLOSE")
                except (OSError, ValueError):
                    pass
                self._event({"event": "browser_transport_disconnected",
                             "session_id": session_id})
                self._active_session = None
                self._active_writer = None

    @staticmethod
    def _accept_pad_batch(message: Any, session_id: str, last_frame: int,
                          frame_total: int) -> tuple[int, int, list[tuple[int, str]]] | str:
        if not isinstance(message, dict) or set(message) != {
            "type", "sessionId", "playerPort", "frames"
        }:
            return "unexpected message fields"
        player_port = message.get("playerPort")
        if (message.get("type") != "pad_batch" or message.get("sessionId") != session_id
                or isinstance(player_port, bool) or not isinstance(player_port, int)
                or player_port != 1):
            return "session or player port mismatch"
        frames = message.get("frames")
        if (not isinstance(frames, list) or not 1 <= len(frames) <= MAX_BATCH_FRAMES
                or frame_total + len(frames) > MAX_SESSION_FRAMES):
            return "PAD batch size limit"
        accepted: list[tuple[int, str]] = []
        previous = last_frame
        for item in frames:
            if not isinstance(item, dict) or set(item) != {"frame", "padHex"}:
                return "unexpected PAD frame fields"
            frame = item.get("frame")
            payload_hex = item.get("padHex")
            if (isinstance(frame, bool) or not isinstance(frame, int) or
                    not 1 <= frame <= 1_000_000 or frame <= previous):
                return "duplicate, out-of-order, or invalid frame tag"
            if not isinstance(payload_hex, str) or not PAD_HEX.fullmatch(payload_hex):
                return "PAD data must contain exactly eight bytes"
            accepted.append((frame, payload_hex.lower()))
            previous = frame
        return previous, frame_total + len(accepted), accepted

    async def _pump_relay_events(self, reader: asyncio.StreamReader,
                                 writer: asyncio.StreamWriter, session_id: str) -> None:
        offset = 0
        pending = bytearray()
        while not reader.at_eof() and self._active_session == session_id:
            try:
                with self.relay_event_log.open("rb") as stream:
                    stream.seek(offset)
                    chunk = stream.read(8192)
                offset += len(chunk)
                pending.extend(chunk)
                if len(pending) > 8192:
                    return
                while b"\n" in pending:
                    line, _, rest = pending.partition(b"\n")
                    pending = bytearray(rest)
                    if not line or len(line) > MAX_WEBSOCKET_MESSAGE_BYTES:
                        continue
                    try:
                        event = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if (not isinstance(event, dict) or event.get("session_id") != session_id
                            or event.get("event") not in CLIENT_EVENTS):
                        continue
                    await self._send_json(writer, event)
                    self._event({"event": "browser_transport_event_forwarded",
                                 "session_id": session_id, "relay_event": event["event"],
                                 "frame": event.get("frame")})
            except FileNotFoundError:
                pass
            await asyncio.sleep(0.005)

    async def _serve_page(self, writer: asyncio.StreamWriter) -> None:
        body = (HERE / "browser_transport.html").read_bytes()
        response = (
            b"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            + f"Content-Length: {len(body)}\r\n".encode("ascii")
            + b"Cache-Control: no-store\r\nConnection: close\r\n"
            + b"Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; "
              b"connect-src ws://127.0.0.1:"
            + str(self.port).encode("ascii")
            + b"; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'\r\n\r\n"
            + body
        )
        writer.write(response)
        await writer.drain()

    @staticmethod
    async def _http_reply(writer: asyncio.StreamWriter, status: int, body: bytes) -> None:
        reason = {400: "Bad Request", 403: "Forbidden", 404: "Not Found", 409: "Conflict"}.get(
            status, "Service Unavailable")
        writer.write(
            f"HTTP/1.1 {status} {reason}\r\n".encode("ascii")
            + b"Content-Type: text/plain; charset=utf-8\r\nConnection: close\r\n"
            + f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body
        )
        await writer.drain()

    @staticmethod
    async def _send_json(writer: asyncio.StreamWriter, value: dict[str, Any]) -> None:
        writer.write(_server_frame(json.dumps(value, sort_keys=True,
                                              separators=(",", ":")).encode("utf-8")))
        await writer.drain()

    @staticmethod
    async def _close_policy(writer: asyncio.StreamWriter, reason: str) -> None:
        payload = struct.pack(">H", 1008) + reason.encode("utf-8")[:120]
        writer.write(_server_frame(payload, 0x8))
        await writer.drain()

    async def serve_forever(self) -> None:
        if self._server is None:
            await self.start()
        assert self._server is not None
        async with self._server:
            await self._stopping.wait()

    def stop(self) -> None:
        self._stopping.set()
        if self._input_fd >= 0:
            os.close(self._input_fd)
            self._input_fd = -1
        if self._event_fd >= 0:
            os.close(self._event_fd)
            self._event_fd = -1


async def _main(args: argparse.Namespace) -> None:
    server = BrowserTransportServer(Path(args.input_log), Path(args.relay_event_log),
                                    Path(args.service_event_log), port=args.port)
    await server.start()
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stop_event.set)
        except NotImplementedError:
            pass
    print(f"browser transport ready at {server.origin}", flush=True)
    try:
        await stop_event.wait()
    finally:
        server.stop()
        assert server._server is not None
        server._server.close()
        await server._server.wait_closed()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-log", required=True)
    parser.add_argument("--relay-event-log", required=True)
    parser.add_argument("--service-event-log", required=True)
    parser.add_argument("--port", type=int, default=43116)
    args = parser.parse_args(argv)
    try:
        asyncio.run(_main(args))
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
