#!/usr/bin/env python3
"""Receive and decode BreathSense BLE notifications on Raspberry Pi."""

from __future__ import annotations

import asyncio
import argparse
import json
import struct
import time
from datetime import datetime, timezone

from bleak import BleakClient, BleakScanner


DEVICE_NAME = "MyDevice_01"

COUGH_EVENT_UUID = "b5e00002-7a4b-4c6d-9e10-112233445566"
ENVIRONMENT_UUID = "b5e00003-7a4b-4c6d-9e10-112233445566"
TIME_SYNC_UUID = "b5e00004-7a4b-4c6d-9e10-112233445566"

FLAG_TIMESTAMP_VALID = 1 << 0
MIN_EPOCH = 1577836800
MAX_EPOCH = 4102444799
FLAG_STAGE2_VALID = 1 << 1
FLAG_PROLONGED = 1 << 2
DURATION_SHIFT = 3
DURATION_MASK = 0x1F

COUGH_TYPES = {
    0: "UNKNOWN",
    1: "DRY",
    2: "WET",
}


def utc_now_text() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def decode_cough_event(data: bytearray) -> dict[str, object]:
    if len(data) != 8:
        raise ValueError(f"cough payload must be 8 bytes, received {len(data)}")

    flags = data[0]
    cough_type_value = data[1]
    start_time_s = int.from_bytes(data[2:6], "little", signed=False)
    event_counter = int.from_bytes(data[6:8], "little", signed=False)

    timestamp_valid = bool(flags & FLAG_TIMESTAMP_VALID) and MIN_EPOCH <= start_time_s <= MAX_EPOCH
    stage2_valid = bool(flags & FLAG_STAGE2_VALID)
    prolonged = bool(flags & FLAG_PROLONGED)
    duration_s = (flags >> DURATION_SHIFT) & DURATION_MASK

    event: dict[str, object] = {
        "message": "breathsense_cough_event",
        "received_at_utc": utc_now_text(),
        "event_counter": event_counter,
        "timestamp_valid": timestamp_valid,
        "cough_type": COUGH_TYPES.get(cough_type_value, "INVALID"),
        "stage2_valid": stage2_valid,
        "prolonged": prolonged,
        "duration_s": duration_s,
        "raw_hex": data.hex(" "),
    }

    if timestamp_valid:
        event["timestamp_source"] = "node_unix_seconds"
        event["bout_start_utc"] = datetime.fromtimestamp(
            start_time_s,
            timezone.utc,
        ).isoformat(timespec="seconds")
    else:
        event["bout_start_utc"] = None
        event["timestamp_source"] = "unknown_unsynchronized"

    return event


def decode_environment(data: bytearray) -> dict[str, object]:
    if len(data) != 4:
        raise ValueError(
            f"environment payload must be 4 bytes, received {len(data)}"
        )

    temperature_x100 = int.from_bytes(data[0:2], "little", signed=True)
    humidity_x100 = int.from_bytes(data[2:4], "little", signed=False)

    return {
        "message": "breathsense_environment",
        "received_at_utc": utc_now_text(),
        "temperature_c": temperature_x100 / 100.0,
        "humidity_percent": humidity_x100 / 100.0,
        "raw_hex": data.hex(" "),
    }


def print_event(event: dict[str, object]) -> None:
    print(json.dumps(event, ensure_ascii=False), flush=True)


def on_cough_notification(_characteristic: object, data: bytearray) -> None:
    try:
        event = decode_cough_event(data)
        print_event(event)

        if event["prolonged"]:
            print(
                "ALERT: prolonged cough bout "
                f"({event['duration_s']} seconds)",
                flush=True,
            )
    except (ValueError, OverflowError, OSError) as error:
        print(f"Invalid cough notification: {error}", flush=True)


def on_environment_notification(
    _characteristic: object,
    data: bytearray,
) -> None:
    try:
        print_event(decode_environment(data))
    except ValueError as error:
        print(f"Invalid environment notification: {error}", flush=True)


async def synchronize_time(client: BleakClient) -> None:
    epoch_s = int(time.time())
    if not MIN_EPOCH <= epoch_s <= MAX_EPOCH:
        raise RuntimeError("Set the Raspberry Pi UTC clock before using BreathSense")
    await client.write_gatt_char(TIME_SYNC_UUID, struct.pack("<I", epoch_s), response=True)
    readback = bytes(await client.read_gatt_char(TIME_SYNC_UUID))
    if len(readback) != 4:
        raise RuntimeError("Invalid Time Sync readback")
    device_epoch_s = struct.unpack("<I", readback)[0]
    if abs(device_epoch_s - int(time.time())) > 5:
        raise RuntimeError(f"Clock verification failed: device={device_epoch_s}")
    print(f"BLE peer clock synchronized: pi={epoch_s}, peer={device_epoch_s}", flush=True)


async def receive_until_disconnected(device_name: str = DEVICE_NAME) -> None:
    print(f"Scanning for {device_name}...", flush=True)
    device = await BleakScanner.find_device_by_name(device_name, timeout=15.0)

    if device is None:
        raise RuntimeError(f"BLE device {device_name!r} was not found")

    disconnected = asyncio.Event()

    def on_disconnected(_client: BleakClient) -> None:
        disconnected.set()

    async with BleakClient(
        device,
        disconnected_callback=on_disconnected,
        timeout=20.0,
    ) as client:
        print(f"Connected to {device.address}", flush=True)

        await client.start_notify(COUGH_EVENT_UUID, on_cough_notification)
        await client.start_notify(
            ENVIRONMENT_UUID,
            on_environment_notification,
        )

        # Required order: subscribe first, then install the new epoch. Firmware
        # begins flushing its offline cough queue only after this write.
        while not disconnected.is_set():
            await synchronize_time(client)
            # Relay readback verifies its own clock; node ACK is reported
            # separately by RELAY status bit 7 and the source sync log.
            try:
                await asyncio.wait_for(disconnected.wait(), timeout=3600)
            except asyncio.TimeoutError:
                pass


async def main(device_name: str = DEVICE_NAME) -> None:
    while True:
        try:
            await receive_until_disconnected(device_name)
        except asyncio.CancelledError:
            raise
        except Exception as error:
            print(f"BLE receiver error: {error}", flush=True)

        print("Reconnecting in 2 seconds...", flush=True)
        await asyncio.sleep(2.0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default=DEVICE_NAME, help="MyDevice_01 or BSRelay_01")
    args = parser.parse_args()
    try:
        asyncio.run(main(args.name))
    except KeyboardInterrupt:
        print("Stopped")
