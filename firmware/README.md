# Mini Printer Firmware

ESP32 firmware for a 384-dot thermal mini printer.

The firmware keeps hardware control, print scheduling, protocol parsing, and
network transports separate so the printer core is independent of BLE or Wi-Fi.

## Architecture

```text
BLE ─────┐
         ├── print protocol ── print service ── printer HAL
Wi-Fi ───┘                         │
                              FreeRTOS queue
```

- `hal/` owns GPIO, SPI, motor, sensors, and print-head timing.
- `core/` owns print jobs, session ownership, and the bounded FreeRTOS queue.
- `protocol/` parses a transport-independent byte stream.
- `transport/` adapts BLE and Wi-Fi/TCP to the same protocol.

The print service streams lines through a 16-entry queue instead of buffering a
whole image in RAM.

## Build

This is a PlatformIO project:

```sh
pio run
```

## BLE

The firmware exposes the existing service and characteristic UUIDs. Writes use
the framed print protocol documented in [docs/protocol.md](docs/protocol.md).
The same characteristic returns/notifications status frames.

## Wi-Fi

Wi-Fi printing is enabled by default as a SoftAP.

The SSID is:

```text
Mini-Printer-XXXX
```

where `XXXX` is derived from the ESP32 ID. Development builds use an open AP
unless `WIFI_AP_PASSWORD` is configured.

The printer accepts the same protocol over TCP port `9100`.

## Hardware

The current pin map targets the original V4 board configuration. Print-head
timing and thermistor calibration are intentionally kept in the hardware layer
so they can be validated and tuned independently of protocol/network changes.

The GPIO 5 button prints a built-in diagnostic page on a double click. Holding
it for about one second feeds paper, and releasing it stops the feed.
