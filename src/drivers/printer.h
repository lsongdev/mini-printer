#ifndef PRINTER_H
#define PRINTER_H

#include "drivers/phead.h"

#define MAX_NLINES 400

void printer_init();
void printer_send(uint8_t data[MAX_NLINES][NBYTES_PER_LINE], uint16_t nlines);

#endif