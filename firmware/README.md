# Mini Printer Firmware

ESP32 firmware for a 384-dot thermal mini printer.

The firmware keeps hardware control, print scheduling, protocol parsing,
network management, and transports separate. BLE, TCP, and HTTP all converge
on the same print protocol and print service.

## Architecture

```text
BLE ───────────┐
TCP :9100 ─────┼── MP protocol ── print service ── printer HAL
HTTP /api/print┘                      │
                                  FreeRTOS queue

Wi-Fi STA ────── wifi_manager ── HTTP :80 / TCP :9100 / mDNS
                    │
                    └── temporary AP+STA setup mode
```

- `hal/` owns GPIO, SPI, motor, sensors, and print-head timing.
- `core/` owns print jobs, session ownership, and the bounded FreeRTOS queue.
- `protocol/` parses a transport-independent byte stream.
- `transport/` adapts BLE and raw TCP to the same MP protocol.
- `network/` owns Wi-Fi state, AP setup, and the HTTP UI/API.

The print service streams lines through a 16-entry queue instead of buffering a
whole image in RAM.

## Build

This is a PlatformIO project:

```sh
pio run
pio test -e native
```

## BLE

The default `esp32dev` build enables BLE, Wi-Fi, HTTP, and TCP together. Normal operation uses STA only; AP+STA is enabled temporarily for provisioning and recovery.
It starts a BLE-only controller and releases unused Bluetooth Classic controller
memory before initializing the BLE stack.

ESP32 shares one 2.4 GHz radio between Wi-Fi and Bluetooth, so the controller
time-slices radio access. Espressif lists Wi-Fi STA plus BLE as stable, while
SoftAP with a connected client plus BLE is supported with reduced stability.
On this printer, the earlier combined build also started the unused dual-mode
Bluetooth controller. Its Wi-Fi connected heap was about 32 KB with an 18 KB
largest allocation; selecting BLE-only raised these to about 52 KB and 33 KB.
In a concurrent run, 60/60 LAN HTTP requests and 60/60 BLE status requests
succeeded; a BLE test page printed and returned to idle. SoftAP access from a
phone remained stable during the same testing session.

The firmware exposes the existing service and characteristic UUIDs. Writes use
the framed print protocol documented in [`../docs/protocol.md`](../docs/protocol.md). The same
characteristic returns/notifications status frames.

## Wi-Fi

Normal operation uses `WIFI_STA` only. The setup SoftAP exists only while the
printer is being provisioned or recovered.

Startup behavior is:

```text
no saved network
    -> setup AP

saved network
    -> STA connect for up to 30 seconds
       -> success: online, STA only
       -> failure: setup AP
```

The setup AP SSID is:

```text
Mini-Printer-XXXX
```

where `XXXX` is derived from the ESP32 ID. Development builds use an open AP
unless `WIFI_AP_PASSWORD` is configured.

A triple click on GPIO 5 requests setup mode at any time. If saved credentials
exist, setup mode is a temporary five-minute recovery window. A first-boot
device with no credentials remains in setup until configured. After a successful
connection the HTTP response is sent first, then the AP closes after a short
delay and the printer returns to STA-only operation.

While setup mode is active:

- captive DNS points hostnames at the setup AP
- `/api/wifi/scan`, `/api/wifi/connect`, and `/api/wifi/forget` are enabled
- the embedded Web UI can still print through `POST /api/print`
- raw MP printing on TCP port `9100` remains available

The AP is still temporary; successful provisioning or the setup timeout returns
the printer to STA-only operation.

The printer stores STA credentials as one NVS record only after a successful
connection. A failed setup attempt is not persisted. During normal operation,
a transient STA disconnect relies on automatic reconnect and does **not** open
the setup AP.

Normal network services are:

- HTTP on port `80`
- MP protocol over raw TCP on port `9100`
- mDNS as `mini-printer-xxxx.local`

During setup, connect to `Mini-Printer-XXXX`. Captive DNS redirects generic
HTTP probes to the embedded page; `http://192.168.4.1/` is also available.

The TCP service is not IPP, AirPrint, JetDirect, or ESC/POS. It carries the
project's MP framing protocol.

## Web UI

Opening the printer over HTTP shows a fully offline page embedded in firmware.
It does not depend on a CDN or Internet access.

The page has separate Print and Wi-Fi tabs. It can:

- show printer and Wi-Fi status
- scan nearby Wi-Fi networks
- connect to or forget a saved Wi-Fi network
- show which network is saved for restart
- select an image and print it

Image rasterization happens in the browser. Canvas scales the source image to
384 dots, converts it to a 1-bit raster, and sends the resulting MP stream to
`POST /api/print`. The ESP32 therefore does not need PNG/JPEG decoders or a
large page buffer.

### HTTP API

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/` | Embedded setup and print page |
| `GET` | `/api/status` | Printer and Wi-Fi status |
| `GET` | `/api/wifi/scan` | Scan nearby networks |
| `POST` | `/api/wifi/connect` | Connect using URL-encoded `ssid` and `password` |
| `POST` | `/api/wifi/forget` | Forget the saved STA network |
| `POST` | `/api/print` | Stream an `application/octet-stream` MP print job |

`/api/print` feeds request chunks directly into the existing MP parser. It does
not buffer the entire print job in firmware.

## Hardware

The current pin map targets the original V4 board configuration. Print-head
timing and thermistor calibration are intentionally kept in the hardware layer
so they can be validated and tuned independently of protocol/network changes.

GPIO 5 uses the shared `kbutton.c` gesture state machine. A double click prints
a built-in diagnostic page, a triple click enters Wi-Fi setup mode, and holding
the button for about one second feeds paper until release.
