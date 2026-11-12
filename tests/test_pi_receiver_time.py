import asyncio
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest.mock import AsyncMock, patch

# Radio-free tests do not require the platform's Bluetooth dependencies.
fake_bleak = types.ModuleType("bleak")
fake_bleak.BleakClient = object
fake_bleak.BleakScanner = object
sys.modules.setdefault("bleak", fake_bleak)

spec = importlib.util.spec_from_file_location("pi_receiver", Path(__file__).parents[1] / "tools/pi_ble_receiver.py")
receiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver)


class ReceiverTimeTests(unittest.TestCase):
    def test_10am_cough_arrives_at_1005(self):
        with patch.object(receiver, "utc_now_text", return_value="2026-11-12T03:05:00+00:00"):
            event = receiver.decode_cough_event(struct.pack("<BBIH", 1, 1, 1794452400, 42))
        self.assertEqual("2026-11-12T03:00:00+00:00", event["bout_start_utc"])
        self.assertEqual("2026-11-12T03:05:00+00:00", event["received_at_utc"])

    def test_invalid_flag_or_epoch_remains_unknown(self):
        for flags,epoch in [(0,1794452400),(1,0),(1,1)]:
            event = receiver.decode_cough_event(struct.pack("<BBIH",flags,1,epoch,42))
            self.assertIsNone(event["bout_start_utc"])
            self.assertEqual("unknown_unsynchronized",event["timestamp_source"])

    def test_sync_verifies_readback(self):
        peer = AsyncMock()
        peer.read_gatt_char.return_value = struct.pack("<I",1794452700)
        with patch.object(receiver.time,"time",return_value=1794452700), contextlib.redirect_stdout(io.StringIO()):
            asyncio.run(receiver.synchronize_time(peer))
        peer.write_gatt_char.assert_awaited_once_with(receiver.TIME_SYNC_UUID,
            struct.pack("<I",1794452700), response=True)

    def test_bad_clock_and_readback_fail(self):
        peer = AsyncMock()
        with patch.object(receiver.time,"time",return_value=1), self.assertRaises(RuntimeError):
            asyncio.run(receiver.synchronize_time(peer))
        peer.write_gatt_char.assert_not_awaited()
        for value in (b"",struct.pack("<I",1794452400)):
            peer.read_gatt_char.return_value=value
            with patch.object(receiver.time,"time",return_value=1794452700), self.assertRaises(RuntimeError):
                asyncio.run(receiver.synchronize_time(peer))


if __name__ == "__main__":
    unittest.main()
