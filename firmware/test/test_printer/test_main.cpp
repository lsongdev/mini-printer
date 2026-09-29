#include <unity.h>
#include <string.h>

#include "em_config.h"
#include "hal/em_printer.h"

// Compile the driver against the fake GPIO and hardware dependencies below.
#include "../../src/hal/em_printer.cpp"

namespace {
bool has_paper = true;
bool valid_temperature = true;
float temperature_c = 25.0f;
bool spi_ok = true;
uint32_t now_ms = 0;
int spi_writes = 0;
int motor_steps = 0;
int feed_steps = 0;
bool motor_moved_during_strobe = false;
bool motor_moved_before_six_strobes = false;
uint8_t strobe_pulses = 0;
uint64_t strobe_started_at[6] = {};
uint64_t longest_strobe_us = 0;
int release_count = 0;
uint8_t reported_temperature = 0;
paper_state_e reported_paper = PAPER_STATUS_NORMAL;

void trace_gpio(uint8_t pin, uint8_t value)
{
    for (uint8_t group = 0; group < 6; ++group)
    {
        if (pin != stb_pins[group])
            continue;
        if (value == HIGH)
        {
            strobe_started_at[group] = mock_delay_total_us();
            ++strobe_pulses;
        }
        else if (strobe_started_at[group] != 0)
        {
            const uint64_t width = mock_delay_total_us() - strobe_started_at[group];
            if (width > longest_strobe_us)
                longest_strobe_us = width;
            strobe_started_at[group] = 0;
        }
    }
}
}

uint32_t millis() { return now_ms; }
bool paper_present() { return has_paper; }
bool adc_read_temperature_c(float *value)
{
    if (!valid_temperature)
        return false;
    *value = temperature_c;
    return true;
}
void set_device_temperature(uint8_t value) { reported_temperature = value; }
void set_device_paper_status(paper_state_e value) { reported_paper = value; }
void motor_init() {}
void motor_step()
{
    if (strobe_pulses < 6)
        motor_moved_before_six_strobes = true;
    ++motor_steps;
}
void motor_feed_steps(uint32_t steps)
{
    if (strobe_pulses < 6)
        motor_moved_before_six_strobes = true;
    for (uint8_t pin : stb_pins)
        if (mock_gpio_level(pin) == HIGH)
            motor_moved_during_strobe = true;
    motor_steps += steps;
    feed_steps += steps;
}
void motor_release() { ++release_count; }
void printer_spi_init() {}
bool printer_spi_write(const uint8_t *, size_t length)
{
    ++spi_writes;
    return spi_ok && length == TPH_DI_LEN;
}

void setUp()
{
    mock_gpio_write_hook() = nullptr;
    printer_abort();
    has_paper = true;
    valid_temperature = true;
    temperature_c = 25.0f;
    spi_ok = true;
    now_ms = 0;
    mock_delay_total_us() = 0;
    spi_writes = motor_steps = feed_steps = release_count = 0;
    motor_moved_during_strobe = false;
    motor_moved_before_six_strobes = false;
    strobe_pulses = 0;
    longest_strobe_us = 0;
    for (uint64_t &started : strobe_started_at)
        started = 0;
    reported_temperature = 0;
    reported_paper = PAPER_STATUS_NORMAL;
    printer_init();
    mock_gpio_write_hook() = trace_gpio;
}
void tearDown() {}

void test_line_is_safe_after_completion()
{
    uint8_t line[TPH_DI_LEN];
    memset(line, 0xff, sizeof(line));
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_begin());
    TEST_ASSERT_EQUAL_INT(HIGH, mock_gpio_level(PIN_VHEN));
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_write_line(line, 0));
    TEST_ASSERT_EQUAL_INT(1, spi_writes);
    TEST_ASSERT_EQUAL_INT(4, motor_steps);
    TEST_ASSERT_EQUAL_UINT8(6, strobe_pulses);
    TEST_ASSERT_FALSE(motor_moved_during_strobe);
    TEST_ASSERT_FALSE(motor_moved_before_six_strobes);
    TEST_ASSERT_TRUE(longest_strobe_us <= 3000);
    TEST_ASSERT_EQUAL_INT(HIGH, mock_gpio_level(PIN_VHEN));
    TEST_ASSERT_EQUAL_INT(HIGH, mock_gpio_level(PIN_LAT));
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_end(16));
    TEST_ASSERT_EQUAL_INT(LOW, mock_gpio_level(PIN_VHEN));
    TEST_ASSERT_EQUAL_INT(20, feed_steps);
    TEST_ASSERT_EQUAL(PRINTER_ERROR_PROTOCOL, printer_write_line(line, 1));
}

void test_missing_paper_and_sensor_fault_fail_closed()
{
    has_paper = false;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NO_PAPER, printer_begin());
    TEST_ASSERT_EQUAL_INT(LOW, mock_gpio_level(PIN_VHEN));
    TEST_ASSERT_EQUAL(PAPER_STATUS_LACK, reported_paper);

    has_paper = true;
    valid_temperature = false;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_TEMPERATURE_SENSOR, printer_begin());
    TEST_ASSERT_EQUAL_UINT8(255, reported_temperature);

    valid_temperature = true;
    temperature_c = 70.0f;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_OVERHEAT, printer_begin());
}

void test_blank_line_keeps_motor_cadence_without_heating()
{
    uint8_t line[TPH_DI_LEN] = {};
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_begin());
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_write_line(line, 0));
    TEST_ASSERT_EQUAL_INT(4, motor_steps);
    TEST_ASSERT_TRUE(mock_delay_total_us() >= 6 * PRINT_END_TIME + 6 * PRINT_TIME / 2);
    for (uint8_t pin : stb_pins)
        TEST_ASSERT_EQUAL_INT(LOW, mock_gpio_level(pin));
}

void test_spi_failure_and_timeout_abort_head()
{
    uint8_t line[TPH_DI_LEN] = {};
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_begin());
    spi_ok = false;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_PROTOCOL, printer_write_line(line, 0));
    TEST_ASSERT_EQUAL_INT(LOW, mock_gpio_level(PIN_VHEN));
    TEST_ASSERT_EQUAL_INT(0, motor_steps);

    spi_ok = true;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_begin());
    now_ms = PRINTER_STALL_TIMEOUT_MS;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_TIMEOUT, printer_poll());
    TEST_ASSERT_EQUAL(PRINTER_ERROR_PROTOCOL, printer_poll());
}

void test_paper_loss_stops_trailing_and_manual_feed()
{
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_begin());
    has_paper = false;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NO_PAPER, printer_end(16));
    TEST_ASSERT_EQUAL_INT(0, feed_steps);

    has_paper = true;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_feed_start());
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NONE, printer_feed_step());
    TEST_ASSERT_EQUAL_INT(1, motor_steps);
    has_paper = false;
    TEST_ASSERT_EQUAL(PRINTER_ERROR_NO_PAPER, printer_feed_step());
    TEST_ASSERT_EQUAL_INT(LOW, mock_gpio_level(PIN_VHEN));
    TEST_ASSERT_EQUAL(PRINTER_ERROR_PROTOCOL, printer_feed_step());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_line_is_safe_after_completion);
    RUN_TEST(test_missing_paper_and_sensor_fault_fail_closed);
    RUN_TEST(test_blank_line_keeps_motor_cadence_without_heating);
    RUN_TEST(test_spi_failure_and_timeout_abort_head);
    RUN_TEST(test_paper_loss_stops_trailing_and_manual_feed);
    return UNITY_END();
}
