#include <Arduino.h>
#include "driver/adc.h"
#include "driver/gpio.h"

void setup()
{
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");

  adc_init();
  gpio_init();
}

void loop()
{

  gpio_led_set_status(LED_FAST);

  int battery = adc_battery_percent();
  Serial.printf("battery = %d%\n", battery);

  int paper = gpio_paper_check();
  Serial.printf("paper = %d%\n", paper);

  int temp = adc_temp_c();
  Serial.printf("temp = %f\n", temp);
}