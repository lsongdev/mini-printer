"""Print a small BLE test page using this repository's MP protocol.

Install: python3 -m pip install bleak
Run:     python3 python/main.py
Inspect: python3 python/main.py --dry-run
"""

from __future__ import annotations

import argparse
import asyncio
import struct
from dataclasses import dataclass
from typing import Iterable

SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
DEVICE_NAME = "Mini-Printer"

VERSION = 1
BEGIN = 0x01
LINE = 0x02
END = 0x03
CANCEL = 0x04
STATUS_REQUEST = 0x10
STATUS = 0x80
WIDTH = 384
LINE_BYTES = WIDTH // 8
PAGE_LINES = 96

GLYPHS = {
    "B": (30, 17, 17, 30, 17, 17, 30),
    "E": (31, 16, 16, 30, 16, 16, 31),
    "L": (16, 16, 16, 16, 16, 16, 31),
    "T": (31, 4, 4, 4, 4, 4, 4),
    "S": (15, 16, 16, 14, 1, 1, 30),
}


@dataclass(frozen=True)
class PrinterStatus:
    battery: int
    temperature: int
    paper: int
    state: int
    error: int

    def __str__(self) -> str:
        state_names = ("idle", "printing", "feeding", "error")
        state = state_names[self.state] if self.state < len(state_names) else str(self.state)
        paper = "present" if self.paper == 0 else "missing"
        temperature = "invalid" if self.temperature == 255 else f"{self.temperature}°C"
        return (
            f"battery={self.battery}% temperature={temperature} "
            f"paper={paper} state={state} error={self.error}"
        )


def frame(command: int, payload: bytes = b"") -> bytes:
    if len(payload) > LINE_BYTES:
        raise ValueError("Protocol payload exceeds 48 bytes")
    return struct.pack("<2sBBH", b"MP", VERSION, command, len(payload)) + payload


def decode_status(data: bytes | bytearray) -> PrinterStatus:
    if len(data) != 11:
        raise ValueError(f"Expected 11 status bytes, got {len(data)}")
    magic, version, command, length = struct.unpack_from("<2sBBH", data)
    if magic != b"MP" or version != VERSION or command != STATUS or length != 5:
        raise ValueError(f"Unexpected status frame: {bytes(data).hex()}")
    return PrinterStatus(*data[6:11])


def set_dot(line: bytearray, x: int) -> None:
    line[x // 8] |= 0x80 >> (x % 8)


def render_line(y: int) -> bytes:
    line = bytearray(LINE_BYTES)
    if y in (4, PAGE_LINES - 5):
        for x in range(8, WIDTH - 8):
            set_dot(line, x)
        return bytes(line)

    if 4 < y < PAGE_LINES - 5:
        set_dot(line, 8)
        set_dot(line, WIDTH - 9)

    # Five rows per 64-dot heating group; every bar has the same width.
    if 10 <= y < 40:
        group = (y - 10) // 5
        for x in range(group * 64 + 12, group * 64 + 52):
            if x & 1:
                set_dot(line, x)

    if 45 <= y < 66:
        text = "BLE TEST"
        glyph_row = (y - 45) // 3
        for index, character in enumerate(text):
            glyph = GLYPHS.get(character)
            if glyph is None:
                continue
            for column in range(5):
                if glyph[glyph_row] & (0x10 >> column):
                    for scale in range(3):
                        set_dot(line, 120 + index * 18 + column * 3 + scale)

    if y in (70, 71):
        for x in range(12, WIDTH - 12):
            set_dot(line, x)

    if 76 <= y < 92:
        for x in range(16, WIDTH - 16):
            if ((x - 16) // 8 + (y - 76) // 8) % 2 == 0:
                set_dot(line, x)

    return bytes(line)


def page() -> Iterable[bytes]:
    for y in range(PAGE_LINES):
        yield render_line(y)


async def find_printer(timeout: float, identifier: str | None):
    from bleak import BleakScanner

    if identifier:
        device = await BleakScanner.find_device_by_address(identifier, timeout=timeout)
    else:
        device = await BleakScanner.find_device_by_filter(
            lambda device, advertisement: (
                device.name == DEVICE_NAME
                or advertisement.local_name == DEVICE_NAME
                or SERVICE_UUID in advertisement.service_uuids
            ),
            timeout=timeout,
        )
    if device is None:
        raise RuntimeError(f"Could not find {identifier or DEVICE_NAME} in {timeout:g}s")
    return device


async def print_over_ble(args: argparse.Namespace) -> None:
    from bleak import BleakClient

    device = await find_printer(args.scan_timeout, args.device)
    print(f"Found {device.name or DEVICE_NAME}: {device.address}")

    status_events: asyncio.Queue[PrinterStatus] = asyncio.Queue()
    latest_status: PrinterStatus | None = None
    saw_printing = False

    def on_status(_, data: bytearray) -> None:
        nonlocal latest_status, saw_printing
        try:
            status = decode_status(data)
        except ValueError:
            return
        if status != latest_status:
            print(f"Status: {status}")
        latest_status = status
        if status.state == 1:
            saw_printing = True
        status_events.put_nowait(status)

    async with BleakClient(device, timeout=20.0) as client:
        characteristic = client.services.get_characteristic(CHARACTERISTIC_UUID)
        if characteristic is None:
            raise RuntimeError("Printer characteristic is missing")
        if "write" not in characteristic.properties or "notify" not in characteristic.properties:
            raise RuntimeError(f"Unexpected characteristic properties: {characteristic.properties}")

        async def send(command: int, payload: bytes = b"") -> None:
            encoded = frame(command, payload)
            for offset in range(0, len(encoded), args.chunk_size):
                await client.write_gatt_char(
                    characteristic,
                    encoded[offset : offset + args.chunk_size],
                    response=True,
                )

        await client.start_notify(characteristic, on_status)
        initial = decode_status(await client.read_gatt_char(characteristic))
        print(f"Connected: {initial}")
        if initial.paper != 0:
            raise RuntimeError("Printer reports no paper")
        if initial.state in (1, 2):
            raise RuntimeError("Printer is busy")

        while not status_events.empty():
            status_events.get_nowait()

        job_open = False
        try:
            await send(BEGIN, bytes((args.density,)))
            job_open = True
            for number, raster in enumerate(page(), 1):
                await send(LINE, raster)
                if number % 16 == 0 or number == PAGE_LINES:
                    print(f"Sent {number}/{PAGE_LINES} raster lines")
            await send(END)
            job_open = False

            async def wait_for_completion() -> None:
                while True:
                    status = await asyncio.wait_for(status_events.get(), timeout=3.0)
                    if status.state == 3 or status.error != 0:
                        raise RuntimeError(f"Printer reported failure: {status}")
                    if saw_printing and status.state == 0:
                        return
                    if status.state != 1:
                        await send(STATUS_REQUEST)

            await asyncio.wait_for(wait_for_completion(), timeout=args.print_timeout)
            print("BLE print completed; printer returned to idle")
        except BaseException:
            if job_open and client.is_connected:
                try:
                    await send(CANCEL)
                except Exception:
                    pass
            raise
        finally:
            await client.stop_notify(characteristic)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Print a BLE test page on Mini-Printer")
    parser.add_argument("--device", help="BLE address or macOS device UUID")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--print-timeout", type=float, default=45.0)
    parser.add_argument("--density", type=int, default=64)
    parser.add_argument("--chunk-size", type=int, default=20)
    parser.add_argument("--dry-run", action="store_true", help="Check frames without Bluetooth")
    args = parser.parse_args()
    if not 1 <= args.density <= 100:
        parser.error("--density must be 1..100")
    if not 1 <= args.chunk_size <= 20:
        parser.error("--chunk-size must be 1..20 for a baseline BLE MTU")
    return args


def main() -> None:
    args = parse_args()
    if args.dry_run:
        lines = list(page())
        dots = sum(byte.bit_count() for line in lines for byte in line)
        assert len(lines) == PAGE_LINES
        assert all(len(line) == LINE_BYTES for line in lines)
        assert dots > 1000
        print(f"Valid test page: {len(lines)} lines, {LINE_BYTES} bytes/line, {dots} black dots")
        return
    asyncio.run(print_over_ble(args))


if __name__ == "__main__":
    main()
