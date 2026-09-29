# Hardware drivers

The pin map in `src/em_config.h` targets the original V4 board. Each file in
`src/hal/` owns one physical subsystem:

| Driver | Pins and responsibility |
| --- | --- |
| `em_adc` | GPIO 4 enables ADC, GPIO 34 reads the battery divider, GPIO 36 reads the NTC divider |
| `em_button` | GPIO 5 input pull-up, debounce, single/double/long press events |
| `em_indicator` | GPIO 18 LED; the V4 buzzer pin is shared, so buzzer mode requires a different board pin |
| `em_motor` | GPIO 23/22/21/19 stepper coil phases |
| `em_paper` | GPIO 35 paper detector and change interrupt; low means paper present |
| `em_spi` | Print-head HSPI on GPIO 15 clock, GPIO 16 MISO, and GPIO 13 data; transfers one 48-byte raster line through the full-duplex SPI path. GPIO 12 is the latch and must not be selected as SPI MISO. |
| `em_printer` | GPIO 12 latch, six strobe pins, GPIO 17 head boost gate |

`em_task` samples sensors and publishes status. `print_service` serializes all
printing and feeding through one FreeRTOS task. Manual feed steps are issued by
that task; stopping a feed needs no cross-task timer callback. The motor driver
never changes the head boost gate.

The printer driver starts with all strobes and the boost gate low. It checks
paper and thermistor before a job, checks paper on each line, samples the
thermistor every 16 lines, and rejects an invalid or hot sensor reading before
heating. The boost gate rises once at job start, settles for 50 ms, and stays
on until end or abort. Paper loss, sensor fault, inactivity timeout, protocol misuse, or a
rejected SPI write releases the motor and de-energizes the head. The SPI
peripheral has no print-head acknowledgment, so electrical transfer faults
cannot be detected in software. The six strobe groups heat one latched line
before the motor advances four half-steps, as specified in the project hardware
guide. Heat pulses are capped at 3 ms. Blank raster groups keep their timing
slot without energizing a strobe. Trailing feed
checks the paper sensor every eight half-steps.

The NTC divider values and print-head heat curve are the existing V4 estimates.
They need measurement on the actual printer before changing thermal limits or
running long, dense prints. Host tests exercise state and fault paths; they do
not verify voltage, current, or print quality.
