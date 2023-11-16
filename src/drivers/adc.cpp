#include <Arduino.h>

// 电量ADC引脚
#define PIN_BATTERY_ADC 34
#define PIN_HEARER_TEMP_ADC 36

void adc_init()
{
  // 默认分辨率是 12bit，可以不设置
  // analogReadResolution(12);
}

int adc_battery_volt()
{
  // ADC 值 0~4095
  // 因为接了一个分压电阻，所以实际电压为 ADC 值 * 2
  return analogReadMilliVolts(PIN_BATTERY_ADC) * 2;
}

float adc_battery_percent()
{
  int volt = adc_battery_volt();
  Serial.printf("battery volt: %.2fV\n", volt / 1000.0);
  // 电池电量满电为4.2V，没电会在3.3V左右
  return map(volt, 3300, 4200, 0, 100);
}

int adc_temp_c()
{
  const double Vcc = 3.3;   // Supply voltage in volts
  const double R1 = 10.0;   // Pull-up resistance in kilo-ohms
  const float Ka = 273.15;  // Conversion factor from Celsius to Kelvin
  const float Rp = 30.0;    // Pull-up resistance in kilo-ohms (30K ohms)
  const float T2 = Ka + 25; // Reference temperature (25°C in Kelvin)
  const float B = 3950;     // B-constant of the thermistor

  float Vmeasured = analogReadMilliVolts(PIN_HEARER_TEMP_ADC) / 1000.0;
  Serial.printf("heater temp measured volt: %.2fV\n", Vmeasured);
  float rx = R1 * Vmeasured / (Vcc - Vmeasured);
  float tempK = B * T2 / (T2 * log(rx / Rp) + B);
  return tempK - Ka;
}