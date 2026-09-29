#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H

#include <stdint.h>

constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t INPUT = 0;
constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT_PULLUP = 2;
constexpr uint8_t INPUT_PULLDOWN = 3;

inline uint8_t &mock_gpio_level(uint8_t pin)
{
    static uint8_t levels[40] = {};
    return levels[pin];
}
inline uint8_t &mock_gpio_mode(uint8_t pin)
{
    static uint8_t modes[40] = {};
    return modes[pin];
}
inline void pinMode(uint8_t pin, uint8_t mode) { mock_gpio_mode(pin) = mode; }
inline int digitalRead(uint8_t pin) { return mock_gpio_level(pin); }
using mock_write_hook_t = void (*)(uint8_t, uint8_t);
inline mock_write_hook_t &mock_gpio_write_hook()
{
    static mock_write_hook_t hook = nullptr;
    return hook;
}
inline void digitalWrite(uint8_t pin, uint8_t value)
{
    mock_gpio_level(pin) = value;
    if (mock_gpio_write_hook() != nullptr)
        mock_gpio_write_hook()(pin, value);
}
inline uint64_t &mock_delay_total_us()
{
    static uint64_t total = 0;
    return total;
}
inline void delayMicroseconds(uint32_t delay) { mock_delay_total_us() += delay; }
inline void delay(uint32_t delay_ms) { mock_delay_total_us() += delay_ms * 1000u; }
uint32_t millis();

#endif
