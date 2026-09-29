"""Print a small BLE test page using this repository's MP protocol.

Install: python3 -m pip install bleak
Run:     python3 python/main.py
Inspect: python3 python/main.py --dry-run
"""

from __future__ import annotations

import argparse
import asyncio
import struct
import time
from dataclasses import dataclass
from typing import Awaitable, Callable, Iterable

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
LINES_PER_BATCH = 8

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
    expected = {BEGIN: 1, LINE: LINE_BYTES, END: 0, CANCEL: 0, STATUS_REQUEST: 0}
    if command not in expected or len(payload) != expected[command]:
        raise ValueError(f"Invalid command or payload length: {command:#x}, {len(payload)}")
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


def line_batches() -> Iterable[tuple[int, bytes]]:
    """Bound each BLE write to eight complete line frames (432 bytes)."""
    for start in range(0, PAGE_LINES, LINES_PER_BATCH):
        end = min(start + LINES_PER_BATCH, PAGE_LINES)
        yield end, b"".join(frame(LINE, render_line(y)) for y in range(start, end))


def write_chunks(data: bytes, chunk_size: int) -> Iterable[bytes]:
    for offset in range(0, len(data), chunk_size):
        yield data[offset : offset + chunk_size]


async def wait_for_completion(
    events: asyncio.Queue[PrinterStatus],
    request_status: Callable[[], Awaitable[None]],
) -> None:
    observed_printing = False
    while True:
        try:
            status = await asyncio.wait_for(events.get(), timeout=3.0)
        except TimeoutError:
            await request_status()
            continue
        if status.state == 3 or status.error != 0:
            raise RuntimeError(f"Printer reported failure: {status}")
        if status.state == 1:
            observed_printing = True
        if observed_printing and status.state == 0:
            return
        if status.state != 1:
            await request_status()


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

        max_chunk = min(512, client.mtu_size - 3)
        chunk_size = args.chunk_size or max_chunk
        if not 1 <= chunk_size <= max_chunk:
            raise RuntimeError(f"Chunk size must be 1..{max_chunk} for MTU {client.mtu_size}")
        print(f"BLE MTU={client.mtu_size}, write chunk={chunk_size} bytes")

        async def write(data: bytes) -> None:
            for chunk in write_chunks(data, chunk_size):
                await client.write_gatt_char(
                    characteristic,
                    chunk,
                    response=True,
                )

        async def send(command: int, payload: bytes = b"") -> None:
            await write(frame(command, payload))

        await client.start_notify(characteristic, on_status)
        initial = decode_status(await client.read_gatt_char(characteristic))
        print(f"Connected: {initial}")
        if initial.paper != 0:
            raise RuntimeError("Printer reports no paper")
        if initial.state in (1, 2):
            raise RuntimeError("Printer is busy")

        while not status_events.empty():
            status_events.get_nowait()

        job_active = False
        try:
            started_at = time.monotonic()
            await send(BEGIN, bytes((args.density,)))
            job_active = True
            for number, encoded in line_batches():
                await write(encoded)
                if number % 16 == 0 or number == PAGE_LINES:
                    print(f"Sent {number}/{PAGE_LINES} raster lines")
                if saw_printing and latest_status is not None and (
                    latest_status.state == 3 or latest_status.error != 0
                ):
                    raise RuntimeError(f"Printer reported failure: {latest_status}")
            await send(END)
            submitted_at = time.monotonic()
            print(f"Submitted in {submitted_at - started_at:.1f}s")

            await asyncio.wait_for(
                wait_for_completion(status_events, lambda: send(STATUS_REQUEST)),
                timeout=args.print_timeout,
            )
            job_active = False
            print(f"BLE print completed in {time.monotonic() - started_at:.1f}s; printer returned to idle")
        except BaseException:
            if job_active and client.is_connected:
                try:
                    await send(CANCEL)
                except Exception:
                    pass
            raise
        finally:
            if client.is_connected:
                try:
                    await client.stop_notify(characteristic)
                except Exception:
                    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Print a BLE test page on Mini-Printer")
    parser.add_argument("--device", help="BLE address or macOS device UUID")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--print-timeout", type=float, default=45.0)
    parser.add_argument("--density", type=int, default=64)
    parser.add_argument("--chunk-size", type=int, help="Override negotiated MTU-based write size")
    parser.add_argument("--dry-run", action="store_true", help="Check frames without Bluetooth")
    args = parser.parse_args()
    if not 1 <= args.density <= 100:
        parser.error("--density must be 1..100")
    if args.chunk_size is not None and not 1 <= args.chunk_size <= 512:
        parser.error("--chunk-size must be 1..512")
    if args.scan_timeout <= 0 or args.print_timeout <= 0:
        parser.error("timeouts must be positive")
    return args


def main() -> None:
    args = parse_args()
    if args.dry_run:
        lines = list(page())
        dots = sum(bin(byte).count("1") for line in lines for byte in line)
        if len(lines) != PAGE_LINES or any(len(line) != LINE_BYTES for line in lines) or dots <= 1000:
            raise RuntimeError("Invalid test page raster")
        print(f"Valid test page: {len(lines)} lines, {LINE_BYTES} bytes/line, {dots} black dots")
        return
    asyncio.run(print_over_ble(args))


if __name__ == "__main__":
    main()
