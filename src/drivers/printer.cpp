#include <Arduino.h>

#include "drivers/adc.h"
#include "drivers/gpio.h"
#include "drivers/motor.h"
#include "drivers/phead.h"

void printer_init()
{
    phead_init();
}

void printer_send(uint8_t *data) {
    phead_draw_line(data, NBYTES_PER_LINE);
}