#include <Arduino.h>
#include "gpio.h"
#include "heater.h"

#define PIN_STB1 26
#define PIN_STB2 27
#define PIN_STB3 14
#define PIN_STB4 32
#define PIN_STB5 33
#define PIN_STB6 25

#define STB_N 6
#define STB_TIME_COOLING_US 200
#define STB_TIME_HEATING_US 2000

static uint8_t heater_stb_pins[STB_N] = {
    PIN_STB1,
    PIN_STB2,
    PIN_STB3,
    PIN_STB4,
    PIN_STB5,
    PIN_STB6,
};

void heater_power_on()
{
  gpio_heater_power_on();
}

void heater_power_off()
{
  gpio_heater_power_off();
}

void heater_init()
{
  gpio_heater_init();

  // 打印通道, 默认关闭
  for (int i = 0; i < STB_N; i++)
  {
    pinMode(heater_stb_pins[i], OUTPUT);
    digitalWrite(heater_stb_pins[i], LOW);
  }
}

/**
 * 运行加热通道, 加热一段时间后冷却.
 *
 * @param id, 通道id, 取值区间为[0, STB_N)
 */
void heater_run_stb(int id)
{
  if (id < 0 || id >= STB_N)
  {
    Serial.println("ERROR: stb id out of range");
    return;
  }

  digitalWrite(heater_stb_pins[id], HIGH);
  delayMicroseconds(STB_TIME_HEATING_US);
  digitalWrite(heater_stb_pins[id], LOW);
  delayMicroseconds(STB_TIME_COOLING_US);
}

void heater_run_stbs()
{
  for (int i = 0; i < STB_N; i++)
  {
    heater_run_stb(i);
  }
}

/**
 * 关闭全部通道.
 */
void heater_stop_stbs()
{
  for (int i = 0; i < STB_N; i++)
  {
    digitalWrite(heater_stb_pins[i], LOW);
  }
}