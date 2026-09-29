# Architecture

## Layers

```text
transport/
  ble_transport
  wifi_transport
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
Wi-Fi, packet framing, or print-job buffering.

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
[hal.md](hal.md) for the pin ownership and failure behavior.

## Transport model

BLE and Wi-Fi each create a unique session ID and feed bytes into an independent
protocol parser.

The parser emits commands, not printer calls. Transport code therefore cannot
toggle GPIO, drive the motor, or heat the print head directly.

The Wi-Fi implementation is intentionally small: SoftAP plus a raw TCP server on
port 9100. A future configuration UI, STA mode, HTTP endpoint, MQTT transport,
or USB transport can be added without changing the print service or hardware
driver.
