# SPDX-License-Identifier: MIT
from __future__ import annotations

import asyncio
import base64
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from browser_transport_server import (
    BrowserTransportServer,
    MAX_BATCH_FRAMES,
    WEBSOCKET_GUID,
    _read_frame,
)


def _client_frame(payload: bytes, opcode: int = 1) -> bytes:
    mask = b"\x13\x37\x42\x99"
    if len(payload) < 126:
        length = bytes((0x80 | len(payload),))
    elif len(payload) <= 0xFFFF:
        length = bytes((0x80 | 126,)) + struct.pack(">H", len(payload))
    else:
        raise ValueError("test WebSocket payload is too large")
    masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    return bytes((0x80 | opcode,)) + length + mask + masked


async def _read_server_frame(reader: asyncio.StreamReader) -> tuple[int, bytes]:
    first, second = await reader.readexactly(2)
    length = second & 0x7f
    if length == 126:
        length = struct.unpack(">H", await reader.readexactly(2))[0]
    payload = await reader.readexactly(length)
    return first & 0x0f, payload


class BrowserPadMessageTests(unittest.TestCase):
    def test_accepts_only_session_bound_ordered_frame_records(self):
        session = "0123456789abcdef0123456789abcdef"
        result = BrowserTransportServer._accept_pad_batch({
            "type": "pad_batch",
            "sessionId": session,
            "playerPort": 1,
            "frames": [
                {"frame": 140, "padHex": "01007F0000000000"},
                {"frame": 141, "padHex": "01007f0000000000"},
            ],
        }, session, 0, 0)
        self.assertEqual(result, (141, 2, [
            (140, "01007f0000000000"),
            (141, "01007f0000000000"),
        ]))

    def test_rejects_stale_session_other_player_and_extra_fields(self):
        session = "0123456789abcdef0123456789abcdef"
        base = {"type": "pad_batch", "sessionId": session, "playerPort": 1,
                "frames": [{"frame": 1, "padHex": "01007f0000000000"}]}
        stale = {**base, "sessionId": "fedcba9876543210fedcba9876543210"}
        other_player = {**base, "playerPort": 2}
        extra = {**base, "origin": "https://example.invalid"}
        for message in (stale, other_player, extra):
            with self.subTest(message=message):
                self.assertIsInstance(
                    BrowserTransportServer._accept_pad_batch(message, session, 0, 0), str
                )

    def test_rejects_duplicate_out_of_order_and_malformed_payloads(self):
        session = "0123456789abcdef0123456789abcdef"
        for frames in (
            [{"frame": 4, "padHex": "0000000000000000"},
             {"frame": 4, "padHex": "0000000000000000"}],
            [{"frame": 5, "padHex": "0000000000000000"},
             {"frame": 3, "padHex": "0000000000000000"}],
            [{"frame": 1, "padHex": "000000000000000"}],
            [{"frame": True, "padHex": "0000000000000000"}],
        ):
            with self.subTest(frames=frames):
                value = BrowserTransportServer._accept_pad_batch(
                    {"type": "pad_batch", "sessionId": session,
                     "playerPort": 1, "frames": frames}, session, 0, 0
                )
                self.assertIsInstance(value, str)

    def test_caps_batch_and_whole_session_queue(self):
        session = "0123456789abcdef0123456789abcdef"
        oversized_batch = [{"frame": frame, "padHex": "0000000000000000"}
                           for frame in range(1, MAX_BATCH_FRAMES + 2)]
        value = BrowserTransportServer._accept_pad_batch(
            {"type": "pad_batch", "sessionId": session,
             "playerPort": 1, "frames": oversized_batch}, session, 0, 0
        )
        self.assertIsInstance(value, str)
        full_session = [{"frame": frame, "padHex": "0000000000000000"}
                        for frame in range(1, MAX_BATCH_FRAMES + 1)]
        first = BrowserTransportServer._accept_pad_batch(
            {"type": "pad_batch", "sessionId": session,
             "playerPort": 1, "frames": full_session}, session, 0, 0
        )
        self.assertIsInstance(first, tuple)
        second_batch = [{"frame": frame, "padHex": "0000000000000000"}
                        for frame in range(65, 129)]
        second = BrowserTransportServer._accept_pad_batch(
            {"type": "pad_batch", "sessionId": session,
             "playerPort": 1, "frames": second_batch}, session, 64, 64
        )
        self.assertIsInstance(second, tuple)
        next_batch = [{"frame": 129, "padHex": "0000000000000000"}]
        value = BrowserTransportServer._accept_pad_batch(
            {"type": "pad_batch", "sessionId": session,
             "playerPort": 1, "frames": next_batch}, session, 128, 128
        )
        self.assertIsInstance(value, str)


class BrowserTransportWireTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="slippi-browser-transport-test-")
        root = Path(self.temp.name)
        self.input_log = root / "browser-input.log"
        self.relay_log = root / "relay-events.jsonl"
        self.service_log = root / "browser-service-events.jsonl"
        self.server = BrowserTransportServer(self.input_log, self.relay_log,
                                             self.service_log, port=0)
        await self.server.start()

    async def asyncTearDown(self):
        if self.server._active_writer is not None:
            self.server._active_writer.close()
            try:
                await self.server._active_writer.wait_closed()
            except OSError:
                pass
        self.server.stop()
        assert self.server._server is not None
        self.server._server.close()
        await self.server._server.wait_closed()
        self.temp.cleanup()

    async def test_failed_upgrade_releases_session_reservation(self):
        class FailedWriter:
            def write(self, data):
                pass

            async def drain(self):
                # Reservation must precede any write that can yield.
                self_test.assertIsNotNone(self_test.server._active_session)
                raise ConnectionResetError("peer closed during upgrade")

        self_test = self
        headers = {"origin": self.server.origin, "upgrade": "websocket",
                   "connection": "Upgrade", "sec-websocket-version": "13",
                   "sec-websocket-key": base64.b64encode(b"0123456789abcdef").decode()}
        with self.assertRaises(ConnectionResetError):
            await self.server._upgrade(asyncio.StreamReader(), FailedWriter(), headers)
        self.assertIsNone(self.server._active_session)
        self.assertIsNone(self.server._active_writer)
        reader, writer, _, response = await self._connect(self.server.origin)
        self.assertIn(b"101 Switching Protocols", response)
        _, payload = await asyncio.wait_for(_read_server_frame(reader), 2)
        self.assertEqual(json.loads(payload)["event"], "session_ready")
        writer.close()
        await writer.wait_closed()

    async def test_websocket_parser_requires_masked_final_frames(self):
        reader = asyncio.StreamReader()
        reader.feed_data(b"\x81\x01x")
        with self.assertRaises(ValueError):
            await _read_frame(reader)

    async def _connect(self, origin: str):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.server.port)
        key = base64.b64encode(b"0123456789abcdef").decode("ascii")
        request = (
            f"GET /transport HTTP/1.1\r\nHost: 127.0.0.1:{self.server.port}\r\n"
            f"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
            f"Sec-WebSocket-Key: {key}\r\nOrigin: {origin}\r\n\r\n"
        )
        writer.write(request.encode("ascii"))
        await writer.drain()
        headers = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 2)
        return reader, writer, key, headers

    async def test_origin_check_and_browser_to_relay_frame_protocol(self):
        reader, writer, _, headers = await self._connect("http://attacker.invalid")
        self.assertIn(b"403 Forbidden", headers)
        writer.close()
        await writer.wait_closed()

        reader, writer, key, headers = await self._connect(self.server.origin)
        self.assertIn(b"101 Switching Protocols", headers)
        expected_accept = base64.b64encode(
            hashlib.sha1((key + WEBSOCKET_GUID).encode("ascii")).digest()
        )
        self.assertIn(b"Sec-WebSocket-Accept: " + expected_accept, headers)
        opcode, payload = await asyncio.wait_for(_read_server_frame(reader), 2)
        self.assertEqual(opcode, 1)
        session = json.loads(payload)["sessionId"]

        message = {"type": "pad_batch", "sessionId": session, "playerPort": 1,
                   "frames": [{"frame": 140, "padHex": "01007f0000000000"}]}
        writer.write(_client_frame(json.dumps(message).encode("utf-8")))
        await writer.drain()
        opcode, payload = await asyncio.wait_for(_read_server_frame(reader), 2)
        self.assertEqual(opcode, 1)
        self.assertEqual(json.loads(payload)["event"], "pad_queued")
        for _ in range(20):
            if b"PAD " + session.encode("ascii") + b" 140 01007f0000000000\n" in self.input_log.read_bytes():
                break
            await asyncio.sleep(0.01)
        self.assertIn(b"OPEN " + session.encode("ascii") + b"\n", self.input_log.read_bytes())
        self.assertIn(b"PAD " + session.encode("ascii") + b" 140 01007f0000000000\n",
                      self.input_log.read_bytes())

        writer.write(_client_frame(b"", opcode=8))
        await writer.drain()
        await asyncio.wait_for(reader.read(), 2)
        writer.close()
        await writer.wait_closed()
        self.assertIn(b"CLOSE " + session.encode("ascii") + b"\n", self.input_log.read_bytes())

    async def test_malformed_or_replayed_batch_closes_session(self):
        reader, writer, _, headers = await self._connect(self.server.origin)
        self.assertIn(b"101 Switching Protocols", headers)
        _, payload = await asyncio.wait_for(_read_server_frame(reader), 2)
        session = json.loads(payload)["sessionId"]
        message = {"type": "pad_batch", "sessionId": session, "playerPort": 1,
                   "frames": [{"frame": 10, "padHex": "0000000000000000"},
                              {"frame": 9, "padHex": "0000000000000000"}]}
        writer.write(_client_frame(json.dumps(message).encode("utf-8")))
        await writer.drain()
        opcode, payload = await asyncio.wait_for(_read_server_frame(reader), 2)
        self.assertEqual(opcode, 8)
        self.assertEqual(struct.unpack(">H", payload[:2])[0], 1008)
        await asyncio.wait_for(reader.read(), 2)
        writer.close()
        await writer.wait_closed()
        log = self.input_log.read_bytes()
        self.assertNotIn(b"PAD " + session.encode("ascii"), log)
        self.assertIn(b"CLOSE " + session.encode("ascii") + b"\n", log)


if __name__ == "__main__":
    unittest.main()
