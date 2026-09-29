# Print Protocol

The print protocol is a small framed byte-stream protocol. It is deliberately
independent of BLE packet boundaries and TCP reads.

This is a new wire protocol. It intentionally does not preserve the legacy
"one 48-byte GATT write equals one raster line" behavior.

## Frame

All multi-byte integers are little-endian.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | magic: `MP` |
| 2 | 1 | version: `1` |
| 3 | 1 | type |
| 4 | 2 | payload length |
| 6 | N | payload |

The maximum payload is 48 bytes.

## Commands

| Type | Name | Payload |
| ---: | --- | --- |
| `0x01` | BEGIN | 1 byte print density, 0 = default |
| `0x02` | LINE | exactly 48 bytes / 384 dots |
| `0x03` | END | empty |
| `0x04` | CANCEL | empty |
| `0x10` | STATUS_REQUEST | empty |
| `0x80` | STATUS | 5 bytes |

A print job is:

```text
BEGIN
LINE
LINE
...
END
```

The parser accepts frames split across any number of BLE writes or TCP reads.

## Status payload

The `STATUS` payload is:

| Offset | Field |
| ---: | --- |
| 0 | battery percent |
| 1 | temperature °C; `255` means sensor invalid |
| 2 | paper state |
| 3 | printer state |
| 4 | printer error |

Printer states:

- `0`: idle
- `1`: printing
- `2`: manual feed
- `3`: error

Printer errors:

- `0`: none
- `1`: no paper
- `2`: over temperature
- `3`: temperature sensor failure
- `4`: print stall timeout
- `5`: cancelled
- `6`: protocol error

## Sessions and backpressure

A transport connection owns a job from `BEGIN` until `END` or `CANCEL`.
Only one session can own the printer at a time, so BLE and Wi-Fi jobs cannot be
interleaved.

The firmware uses a bounded 16-line FreeRTOS queue. BLE uses acknowledged GATT
writes and TCP uses normal stream flow control, allowing producers to slow down
instead of requiring a large image buffer in the ESP32.
