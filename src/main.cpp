#include <Arduino.h>
#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/motor.h"

void setup()
{
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");

  adc_init();
  gpio_init();
  motor_init();
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

  // motor_run(100);        // Run motor forward 100 steps
  // delay(1000);           // Wait for 1 second
  // motor_run(100, false); // Run motor backward 100 steps
  // delay(1000);           // Wait for 1 second
  // motor_stop();          // Stop the motor
                         // Other loop code...
}