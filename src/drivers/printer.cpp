#include <Arduino.h>

#include "drivers/printer.h"

void printer_init()
{
    phead_init();
}

void printer_send(uint8_t data[MAX_NLINES][NBYTES_PER_LINE], uint16_t nlines)
{
    for (uint16_t line = 0; line < nlines; line++) {
        phead_draw_line(data[line], NBYTES_PER_LINE);
    }
}
