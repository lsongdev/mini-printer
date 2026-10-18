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

## GitHub Pages

Enable **Settings → Pages → Source → GitHub Actions**. The `pages.yml` workflow
tests the client and publishes `www/` on changes to `main` (or manual dispatch).
All asset paths are relative, so the project URL `/mini-printer/` works without
rewrites. The target URL is https://lsongdev.github.io/mini-printer/.

The repository is public so that GitHub Pages is available on the current plan.
