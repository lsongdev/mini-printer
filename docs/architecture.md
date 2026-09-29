# Architecture

## Layers

```text
network/
  wifi_manager ── AP+STA, NVS, captive DNS, mDNS
  web_server   ── HTTP UI/API
        │
transport/
  ble_transport
  tcp_transport
        │
        ▼
protocol/
  print_protocol
        │
        ▼
core/
  print_service
        │
        ▼
hal/
  printer        print head, latch, six strobes, boost gate
  motor          stepper coils
  spi            print head shift register bus
  adc            battery and thermistor inputs
  paper          paper detector and change interrupt
  button         GPIO button and click timing
  indicator      shared board indicator output
```

Dependencies point downward. The hardware layer does not know about BLE,
Wi-Fi, HTTP, packet framing, or print-job buffering.

## Print service

`print_service` owns the printer as a resource.

It provides:

- one active session at a time
- a bounded 16-command FreeRTOS queue
- a dedicated printer task
- serialized access to print-head and motor hardware
- explicit begin/line/end/cancel lifecycle
- manual feed serialized through the same task

A failed sensor check, paper fault, timeout, or cancellation immediately aborts
the hardware and releases the session.

The printer task also advances manual feed at a bounded interval. No timer
callback writes motor GPIO, so print, feed, and stop use the same owner. The
printer driver is the only owner of the print-head boost gate. See
`hal.md` for the pin ownership and failure behavior.

## Transport model

BLE, TCP, and HTTP each create an independent session and feed bytes into the
same protocol/parser path. HTTP request bodies are streamed into the parser as
they arrive rather than being accumulated as a complete page.

The parser emits commands, not printer calls. Transport and network code
therefore cannot toggle GPIO, drive the motor, or heat the print head directly.

Session ranges are separated to make logs easier to read:

- BLE uses low positive session IDs
- HTTP uses IDs with bit 30 set
- TCP uses IDs with bit 31 set
- the built-in test page and manual feed use reserved IDs

Only `print_service` decides whether the printer is available, so BLE, HTTP,
and TCP cannot interleave jobs.

## Network model

`wifi_manager` is separate from printing transports. It owns:

- permanent SoftAP setup/recovery access
- optional STA connection
- validated STA credentials in NVS
- captive DNS for the SoftAP
- mDNS service discovery

The AP stays enabled while STA is connected. The HTTP server and TCP server are
therefore reachable through either interface.

The embedded HTTP page performs image decoding and rasterization in the browser.
The printer firmware receives only 384-dot, 48-byte raster lines through the
normal MP protocol.
