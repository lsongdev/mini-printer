# Mini Printer for WeChat

A native WeChat Mini Program client for the printer's [MP v1 protocol](../docs/protocol.md).
Its single screen follows the iOS client: find and connect to a nearby printer,
read battery, paper and print-head status, choose a photo, set print density,
and print a photo or a test page.

## Open and test

Open this `wxapp/` directory in WeChat Developer Tools and use a physical phone
for BLE discovery and printing. The project keeps the existing AppID in
`project.config.json`; replace it with your own AppID if needed. Bluetooth and
photo access are requested by WeChat when those features are used.

```sh
cd wxapp
npm test
```

The tests verify MP framing, status parsing, photo rasterization, and serialized
BLE writes. They do not replace a print test on a real phone and printer.

## Structure

| Path | Responsibility |
| --- | --- |
| `pages/printer/` | Native single-page interface and print-job state |
| `lib/bluetooth-transport.js` | BLE discovery, connection, notifications, MTU and acknowledged writes |
| `lib/printer-protocol.js` | MP v1 frames, printer UUIDs and status decoding |
| `lib/rasterizer.js` | 384-dot photo dithering and test page |

The transport discovers devices named `Mini-Printer`, verifies the printer's
service and characteristic after connecting, and writes one acknowledged BLE
chunk at a time. It uses the reported ATT MTU when available and falls back to
20-byte chunks. The firmware accepts protocol frames split across BLE writes.

Photo processing uses the Mini Program 2D canvas and caps output at 2048 lines.
The printer receives the same 384-dot raster and print density as the iOS and
Android clients. The app does not use the old OWL protocol or a remote image
processing service.
