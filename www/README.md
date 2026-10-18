# Mini Printer website

Buildless Preact + HTM application using native ESM and an import map. The public
stylesheet is loaded from https://lsong.org/css/stylesheet.css; `style.css` adds
the project layout. Preact and HTM are pinned CDN modules. No install or build is
needed.

## Preview and test

From the repository root:

```sh
python3 -m http.server 8080 --directory www
node --test www/tests/*.test.js
```

Open http://localhost:8080. BLE requires a secure context (HTTPS or localhost)
and Web Bluetooth, typically Android Chrome or desktop Chrome / Edge. A phone
accessing a plain HTTP LAN address does not have a secure context. iOS Safari
does not expose Web Bluetooth; use the repository's native iOS client instead.

## Printing

The workbench supports wrapped Chinese/English text, image upload with optional
ordered dithering, and a diagnostic page. The preview shows the exact 384-dot
raster sent to the device. Images retain their aspect ratio and fit within
384 × 2048 dots. Text exceeding 2048 raster rows is rejected.

`lib/ble.js` handles generic serialized, acknowledged 20-byte GATT writes,
notifications, reads, and connection cleanup. Device UUIDs and MP v1 framing
live in `lib/protocol.js`; job state and completion handling live in
`lib/printer.js`. After BEGIN the client confirms printing state, then streams
lines. After END it waits for fresh idle status before reporting completion.
Cancellation occurs between complete frames; a failed active job is cancelled
when the connection remains available. A timed-out BLE operation disconnects to
avoid reusing a partially written stream. Hardware is required to validate
actual print quality and radio behavior.

Print content stays in the browser and goes directly to the printer. CDN
requests are required to load application dependencies and shared styles.

## Firmware installation

The USB installation panel reads the public GitHub Releases API on page load,
including daily prereleases. `lib/releases.js` selects `mini-printer-full.bin`
(exactly 4 MiB) and `mini-printer-firmware.bin` (within the 0x1e0000-byte
`min_spiffs` app slot). Assets without GitHub's SHA-256 digest are excluded.
New releases appear without redeploying the website. No API token is embedded;
rate limits and an empty release list have retry and GitHub download links.

`lib/firmware.js` loads Espressif's pinned `esptool-js@0.7.0` ESM bundle on
demand. It checks the image size and SHA-256 before opening the serial port,
then checks for a classic ESP32 and at least 4 MiB Flash before writing.
Full installation writes at `0x0` and resets settings/data; application updates
write at `0x10000` and require the existing `min_spiffs` layout.

Automatic download uses the GitHub asset API with `Accept: application/octet-stream`.
If a network or CORS restriction blocks it, download the selected `.bin` and
choose it in the local file picker. Local files must match the selected release's
size and SHA-256. Selected firmware files are not uploaded.

USB flashing requires HTTPS or localhost, Web Serial, and a data-capable USB
cable. Use desktop Chrome or Edge; browsers without Web Serial can still download
images. Finish printing and disconnect BLE before flashing; printing controls
are disabled during flashing. After a full installation, configure Wi-Fi again
through the printer's setup hotspot. Physical USB writes, reset behavior and
printing after installation require hardware verification.

## GitHub Pages

Enable **Settings → Pages → Source → GitHub Actions**. The `pages.yml` workflow
tests the client and publishes `www/` on changes to `main` (or manual dispatch).
All asset paths are relative, so the project URL `/mini-printer/` works without
rewrites. The site uses the account's existing custom domain:
https://lsong.org/mini-printer/ (the default GitHub Pages URL redirects there).

The repository is public so that GitHub Pages is available on the current plan.
