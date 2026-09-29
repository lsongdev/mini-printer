#include "em_adc.h"

#include "em_config.h"

#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
constexpr uint8_t SAMPLE_COUNT = 8;
constexpr float ADC_SUPPLY_V = 3.3f;
constexpr float NTC_SERIES_OHM = 10000.0f;
constexpr float NTC_NOMINAL_OHM = 30000.0f;
constexpr float NTC_NOMINAL_K = 298.15f;
constexpr float NTC_BETA = 3950.0f;
constexpr int BATTERY_MIN_MV = 100;
constexpr int BATTERY_MAX_MV = 3300;
constexpr int NTC_MIN_MV = 10;
constexpr int NTC_MAX_MV = 3290;
SemaphoreHandle_t adc_mutex = nullptr;

bool sample_mv(uint8_t pin, int *millivolts)
{
    if (adc_mutex == nullptr ||
        xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    uint32_t total = 0;
    for (uint8_t i = 0; i < SAMPLE_COUNT; ++i)
        total += analogReadMilliVolts(pin);
    xSemaphoreGive(adc_mutex);
    *millivolts = static_cast<int>(total / SAMPLE_COUNT);
    return true;
}
} // namespace

void adc_init()
{
    if (adc_mutex == nullptr)
        adc_mutex = xSemaphoreCreateMutex();
    pinMode(PIN_ADC_EN, OUTPUT);
    digitalWrite(PIN_ADC_EN, HIGH);
    analogReadResolution(BATTERY_ADC_BIT);
}

bool adc_read_battery_mv(int *millivolts)
{
    if (millivolts == nullptr)
        return false;

    int reading = 0;
    if (!sample_mv(PIN_BATTERY_ADC, &reading))
        return false;
    if (reading < BATTERY_MIN_MV || reading > BATTERY_MAX_MV)
        return false;

    *millivolts = reading;
    return true;
}

bool adc_read_temperature_c(float *temperature)
{
    if (temperature == nullptr)
        return false;

    int millivolts = 0;
    if (!sample_mv(PIN_TEMPERATRUE, &millivolts))
        return false;
    if (millivolts <= NTC_MIN_MV || millivolts >= NTC_MAX_MV)
        return false;

    // NTC is the lower leg of the divider on the V4 board.
    const float voltage = millivolts / 1000.0f;
    const float resistance = voltage * NTC_SERIES_OHM / (ADC_SUPPLY_V - voltage);
    if (!isfinite(resistance) || resistance <= 0.0f)
        return false;

    const float kelvin = 1.0f /
        (logf(resistance / NTC_NOMINAL_OHM) / NTC_BETA + 1.0f / NTC_NOMINAL_K);
    const float celsius = kelvin - 273.15f;
    if (!isfinite(celsius) || celsius < -40.0f || celsius > 150.0f)
        return false;

    *temperature = celsius;
    return true;
}
