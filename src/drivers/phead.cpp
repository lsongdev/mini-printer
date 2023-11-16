#include <Arduino.h>

#include "spi.h"
#include "phead.h"
#include "motor.h"
#include "heater.h"

void phead_init()
{
  spi_init();
  motor_init();
  heater_init();
}

void phead_draw_line(uint8_t *data, int size)
{
  if (size < 0 || size > NBYTES_PER_LINE)
  {
    Serial.println("ERROR: data size out of range");
    return;
  }

  heater_power_on();
  spi_send(data, size);
  motor_run(1);
  heater_run_stbs();
  motor_run(3);
  heater_stop_stbs();
  heater_power_off();
}
