<h1 align="center">Mini Printer</h1>

<p align="center">A 384-dot thermal printer powered by ESP32</p>

<p align="center">
  <img src="printer.png" width="680" alt="Mini Printer internal assembly">
</p>

This repository contains the firmware, test client, hardware notes, and mobile
client source code.

## Project layout

| Path | Contents |
| --- | --- |
| [`firmware/`](firmware/) | PlatformIO firmware: hardware drivers, print queue, protocol parser, BLE, TCP, STA Wi-Fi with temporary setup AP, and embedded Web UI |
| [`python/`](python/) | BLE test-page client and protocol tests |
| [`docs/`](docs/) | Hardware notes, architecture, and [print protocol](docs/protocol.md) |
| [`android/`](android/) · [`ios/`](ios/) · [`wxapp/`](wxapp/) | Client source code for other platforms |
| [`www/`](www/) | Buildless Preact + HTM project website and Web Bluetooth print workbench; GitHub Pages deployment workflow |

`firmware/` is a regular directory in this repository; no Git submodule setup
is needed.

## Build and test

GitHub Actions builds and tests each push and pull request. Successful builds on
`main` publish a `daily-YYYY-MM-DD` prerelease (Asia/Shanghai); later builds on
the same day update that release. You can also trigger the workflow manually.
Download firmware from [Releases](https://github.com/lsongdev/mini-printer/releases):

- `mini-printer-install.zip`: all flashing files, instructions, source snapshot,
  build information, and dependency notices.
- `mini-printer-full.bin`: complete 4 MiB image, flashed at `0x0`; resets saved
  Wi-Fi settings and all other data.
- `mini-printer-firmware.bin`: application image at `0x10000`, for a printer
  already using this project's `min_spiffs` partition layout.
- `mini-printer-build-info.json` and `mini-printer-sha256sums.txt`: source commit,
  dependency versions, and file hashes.

The [project website](https://lsong.org/mini-printer/#install) can install release
firmware over USB using Web Serial in desktop Chrome / Edge. It verifies SHA-256
and checks the ESP32 chip and Flash capacity before writing. If automatic
download is blocked, select the matching downloaded `.bin` in the local picker.

From the repository root, use `make build`, `make test`, or `make package`.
Packaging requires a clean checkout and a Python interpreter with PlatformIO
installed (`make package PYTHON=/path/to/python`). For local validation with
uncommitted changes, use `make package ALLOW_DIRTY=1`; metadata marks the package
as dirty and its source snapshot contains only the committed files.

After installing [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html):

```sh
cd firmware
pio run -e esp32dev
pio test -e native
```

To flash the firmware, replace the serial port with your device's port:

```sh
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-XXXX
```

The BLE test client uses Python and `bleak`:

```sh
python3 -m pip install bleak
python3 python/main.py --dry-run
python3 python/main.py
python3 -m unittest discover -s python -p 'test_*.py'
```

Print data uses the project's compact `MP` frame protocol. The firmware accepts
the same protocol over BLE, raw TCP port `9100`, and HTTP `POST /api/print`.

Wi-Fi normally runs as STA only. On first boot, after a saved-network startup
failure, or after a triple click on the printer button, the temporary
`Mini-Printer-XXXX` setup AP becomes available. Open `http://192.168.4.1/`
(or use the captive portal) to select a network. The AP closes after successful
provisioning. The embedded page can rasterize images in the browser and print
through either the temporary setup AP or normal STA operation.

The TCP endpoint is not IPP or AirPrint. See [`docs/protocol.md`](docs/protocol.md)
for frame fields and status codes, and [`firmware/README.md`](firmware/README.md)
for the network and HTTP API.
