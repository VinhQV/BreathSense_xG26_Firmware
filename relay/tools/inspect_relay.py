"""Read a real relay over BLE. Requires bleak; never writes test cough events."""
import argparse
import asyncio
import json
import struct
import time
from datetime import datetime, timezone
from bleak import BleakClient, BleakScanner

def characteristic(number):
    return "-".join([f"b5e000{number:02x}", "7a4b", "4c6d", "9e10", "112233445566"])

def status(data):
    if len(data) != 20 or data[0] != 1:
        raise ValueError(f"Unexpected relay status: {data.hex()}")
    _, phase, flags, queued, source_rssi, gateway_rssi, rx, tx, dropped, invalid = struct.unpack("<BBBBbbIIIH", data)
    return dict(phase=phase, source_ready=bool(flags & 2), gateway_connected=bool(flags & 4),
                confirmed_delivery=bool(flags & 8), queued=queued, source_rssi=source_rssi,
                relay_clock_valid=bool(flags & 64), source_clock_synced=bool(flags & 128),
                gateway_rssi=gateway_rssi, received=rx, delivered=tx, dropped=dropped, invalid=invalid)

async def run(args):
    device = await BleakScanner.find_device_by_filter(
        lambda d, a: d.address.lower() == args.address.lower() if args.address else a.local_name == "BSRelay_01",
        timeout=15,
    )
    if device is None:
        raise RuntimeError("BSRelay_01 not found. Check power and other gateway connections.")
    print("CONNECT", device.address, flush=True)
    async with BleakClient(device, timeout=20, winrt={"use_cached_services": False}) as client:
        try:
            identity = await client.read_gatt_char(characteristic(8))
            if len(identity) != 8 or identity[0] != 1:
                raise RuntimeError("Invalid source identity")
            print("SOURCE", ":".join(f"{b:02X}" for b in reversed(identity[2:])), flush=True)
        except Exception as exc:
            print("SOURCE_NOT_READY", str(exc), flush=True)
        print("STATUS", json.dumps(status(await client.read_gatt_char(characteristic(7)))), flush=True)
        for number, label in [(5, "PENDANT"), (6, "AI")]:
            try:
                print(label, list(await client.read_gatt_char(characteristic(number))), flush=True)
            except Exception as exc:
                print(label + "_NOT_READY", str(exc), flush=True)
        def on_status(_, data):
            print("STATUS", json.dumps(status(data)), flush=True)
        def on_cough(_, data):
            if len(data) != 8: raise ValueError("Invalid cough payload")
            flags, kind, timestamp, count = struct.unpack("<BBIH", data)
            occurred = (datetime.fromtimestamp(timestamp, timezone.utc).isoformat()
                        if flags & 1 and 1577836800 <= timestamp <= 4102444799 else None)
            print("COUGH", json.dumps(dict(payload=data.hex(), payload_bytes=list(data), flags=flags, kind=kind,
                  timestamp=timestamp, occurred=occurred, counter=count,
                  received=datetime.now(timezone.utc).isoformat())), flush=True)
        def on_environment(_, data):
            if len(data) != 4: raise ValueError("Invalid environment payload")
            temperature, humidity = struct.unpack("<hH", data)
            print("ENVIRONMENT", temperature / 100, humidity / 100, data.hex(), flush=True)
        await client.start_notify(characteristic(7), on_status)
        await client.start_notify(characteristic(2), on_cough, force_indicate=True)
        await client.start_notify(characteristic(3), on_environment)
        if args.sync_time:
            epoch = int(time.time())
            if not 1577836800 <= epoch <= 4102444799:
                raise RuntimeError("Set the host UTC clock before synchronizing")
            await client.write_gatt_char(characteristic(4), struct.pack("<I",epoch), response=True)
            readback = bytes(await client.read_gatt_char(characteristic(4)))
            if len(readback) != 4 or abs(struct.unpack("<I",readback)[0] - int(time.time())) > 5:
                raise RuntimeError("Relay UTC readback failed")
            print("RELAY_CLOCK_VERIFIED; wait for source_clock_synced=true", flush=True)
        await asyncio.sleep(args.seconds)
        print("FINAL", json.dumps(status(await client.read_gatt_char(characteristic(7)))), flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default="")
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--sync-time", action="store_true", help="Install host UTC after subscribing")
    asyncio.run(run(parser.parse_args()))
