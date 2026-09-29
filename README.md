<h1 align="center">Mini Printer</h1>

<p align="center">A 384-dot thermal printer powered by ESP32</p>

<p align="center">
  <img src="printer.png" width="680" alt="Mini Printer internal assembly">
</p>

This repository contains the firmware, test client, hardware notes, and mobile client source code.

## Project layout

| Path | Contents |
| --- | --- |
| [`firmware/`](firmware/) | PlatformIO firmware: hardware drivers, print queue, protocol parser, BLE and Wi-Fi transports |
| [`python/`](python/) | BLE test-page client and protocol tests |
| [`docs/`](docs/) | Hardware notes, architecture, and [print protocol](docs/protocol.md) |
| [`android/`](android/) · [`ios/`](ios/) · [`wxapp/`](wxapp/) | Client source code for other platforms |

`firmware/` is a regular directory in this repository; no Git submodule setup is needed.

## Build and test

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

Print data uses the project's compact `MP` frame protocol. The firmware also accepts the same protocol over TCP port 9100 on its Wi-Fi SoftAP; this is not IPP or AirPrint. See [`docs/protocol.md`](docs/protocol.md) for frame fields and status codes.
