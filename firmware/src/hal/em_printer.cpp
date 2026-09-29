#include "em_printer.h"

#include "em_adc.h"
#include "em_config.h"
#include "em_motor.h"
#include "em_paper.h"
#include "em_spi.h"

#include <math.h>

namespace {
enum class Mode : uint8_t { IDLE, PRINTING, FEEDING };
Mode mode = Mode::IDLE;
uint8_t density_percent = DEFAULT_PRINT_DENSITY;
uint32_t last_line_ms = 0;
constexpr uint8_t STB_COUNT = 6;
constexpr uint8_t BYTES_PER_STB = TPH_DI_LEN / STB_COUNT;
constexpr uint8_t DOTS_PER_STB = TPH_DOTS_PER_LINE / STB_COUNT;
constexpr float MAX_DENSITY_ADD_US = 0.001f * 2040.0f * 2040.0f;
constexpr uint32_t MAX_HEAT_US = 3000;
constexpr uint8_t MOTOR_STEPS_PER_LINE = 4;
const uint8_t stb_pins[STB_COUNT] = {
    PIN_STB1, PIN_STB2, PIN_STB3, PIN_STB4, PIN_STB5, PIN_STB6,
};

void stop_hardware()
{
    for (uint8_t pin : stb_pins)
        digitalWrite(pin, LOW);
    digitalWrite(PIN_VHEN, LOW);
    digitalWrite(PIN_LAT, HIGH);
    motor_release();
    mode = Mode::IDLE;
}

printer_error_e check_sensors(bool check_temperature)
{
    if (!paper_present())
    {
        set_device_paper_status(PAPER_STATUS_LACK);
        return PRINTER_ERROR_NO_PAPER;
    }
    set_device_paper_status(PAPER_STATUS_NORMAL);

    if (!check_temperature)
        return PRINTER_ERROR_NONE;

    float temperature = 0.0f;
    if (!adc_read_temperature_c(&temperature))
    {
        set_device_temperature(255);
        return PRINTER_ERROR_TEMPERATURE_SENSOR;
    }

    const int rounded = static_cast<int>(lroundf(temperature));
    set_device_temperature(static_cast<uint8_t>(rounded < 0 ? 0 : rounded > 254 ? 254 : rounded));
    return temperature >= PRINTER_MAX_TEMPERATURE_C
               ? PRINTER_ERROR_OVERHEAT
               : PRINTER_ERROR_NONE;
}

void heat_group(uint8_t group, uint8_t black_dots)
{
    const float fraction = static_cast<float>(black_dots) / DOTS_PER_STB;
    const float requested_duration =
        (PRINT_TIME + MAX_DENSITY_ADD_US * fraction * fraction) *
        density_percent / 100.0f;
    const uint32_t duration = requested_duration > MAX_HEAT_US
                                  ? MAX_HEAT_US
                                  : static_cast<uint32_t>(requested_duration);
    if (black_dots != 0)
        digitalWrite(stb_pins[group], HIGH);
    // Keep the group timing even when its raster bits are all white.
    delayMicroseconds(duration);
    if (black_dots != 0)
        digitalWrite(stb_pins[group], LOW);
    delayMicroseconds(PRINT_END_TIME);
}

void print_line_phases(const uint8_t *data)
{
    uint8_t black_dots[STB_COUNT] = {};
    for (uint8_t group = 0; group < STB_COUNT; ++group)
        for (uint8_t byte = 0; byte < BYTES_PER_STB; ++byte)
            black_dots[group] += __builtin_popcount(
                static_cast<unsigned int>(data[group * BYTES_PER_STB + byte]));

    for (uint8_t group = 0; group < STB_COUNT; ++group)
        heat_group(group, black_dots[group]);

    // Complete all heater phases before advancing one raster line.
    motor_feed_steps(MOTOR_STEPS_PER_LINE);
}
} // namespace

void printer_init()
{
    // Establish an inactive head before starting SPI or the motor.
    digitalWrite(PIN_VHEN, LOW);
    pinMode(PIN_VHEN, OUTPUT);
    digitalWrite(PIN_LAT, HIGH);
    pinMode(PIN_LAT, OUTPUT);
    for (uint8_t pin : stb_pins)
    {
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
    }
    motor_init();
    printer_spi_init();
    stop_hardware();
}

void printer_set_density(uint8_t density)
{
    density_percent = density == 0 ? DEFAULT_PRINT_DENSITY
                                   : density > 100 ? 100 : density;
}

printer_error_e printer_begin()
{
    stop_hardware();
    const printer_error_e error = check_sensors(true);
    if (error != PRINTER_ERROR_NONE)
        return error;

    digitalWrite(PIN_VHEN, HIGH);
    delay(PRINTER_BOOST_SETTLE_MS);
    last_line_ms = millis();
    mode = Mode::PRINTING;
    return PRINTER_ERROR_NONE;
}

printer_error_e printer_poll()
{
    if (mode != Mode::PRINTING)
        return PRINTER_ERROR_PROTOCOL;

    printer_error_e error = check_sensors(false);
    if (error == PRINTER_ERROR_NONE &&
        static_cast<uint32_t>(millis() - last_line_ms) >= PRINTER_STALL_TIMEOUT_MS)
        error = PRINTER_ERROR_TIMEOUT;
    if (error != PRINTER_ERROR_NONE)
        stop_hardware();
    return error;
}

printer_error_e printer_write_line(const uint8_t *data, uint32_t line_number)
{
    if (mode != Mode::PRINTING || data == nullptr)
    {
        stop_hardware();
        return PRINTER_ERROR_PROTOCOL;
    }

    printer_error_e error = printer_poll();
    if (error != PRINTER_ERROR_NONE)
        return error;
    if (line_number % PRINTER_TEMPERATURE_CHECK_LINES == 0)
    {
        error = check_sensors(true);
        if (error != PRINTER_ERROR_NONE)
        {
            stop_hardware();
            return error;
        }
    }

    if (!printer_spi_write(data, TPH_DI_LEN))
    {
        stop_hardware();
        return PRINTER_ERROR_PROTOCOL;
    }
    digitalWrite(PIN_LAT, LOW);
    delayMicroseconds(LAT_TIME);
    digitalWrite(PIN_LAT, HIGH);

    print_line_phases(data);
    last_line_ms = millis();
    return PRINTER_ERROR_NONE;
}

printer_error_e printer_end(uint32_t feed_steps)
{
    if (mode != Mode::PRINTING)
    {
        stop_hardware();
        return PRINTER_ERROR_PROTOCOL;
    }

    // Never keep the head energized during the trailing feed.
    stop_hardware();
    while (feed_steps != 0)
    {
        if (!paper_present())
        {
            set_device_paper_status(PAPER_STATUS_LACK);
            motor_release();
            return PRINTER_ERROR_NO_PAPER;
        }
        const uint32_t chunk = feed_steps > 8 ? 8 : feed_steps;
        motor_feed_steps(chunk);
        feed_steps -= chunk;
    }
    motor_release();
    return PRINTER_ERROR_NONE;
}

void printer_abort()
{
    stop_hardware();
}

printer_error_e printer_feed_start()
{
    stop_hardware();
    if (!paper_present())
    {
        set_device_paper_status(PAPER_STATUS_LACK);
        return PRINTER_ERROR_NO_PAPER;
    }
    set_device_paper_status(PAPER_STATUS_NORMAL);
    mode = Mode::FEEDING;
    return PRINTER_ERROR_NONE;
}

printer_error_e printer_feed_step()
{
    if (mode != Mode::FEEDING)
        return PRINTER_ERROR_PROTOCOL;
    if (!paper_present())
    {
        set_device_paper_status(PAPER_STATUS_LACK);
        stop_hardware();
        return PRINTER_ERROR_NO_PAPER;
    }
    set_device_paper_status(PAPER_STATUS_NORMAL);
    motor_step();
    return PRINTER_ERROR_NONE;
}

void printer_feed_stop()
{
    stop_hardware();
}
