#ifndef EM_PRINTER_H
#define EM_PRINTER_H

#include "em_device.h"

#include <stdint.h>

constexpr uint16_t TPH_DOTS_PER_LINE = 384;
constexpr uint8_t TPH_DI_LEN = TPH_DOTS_PER_LINE / 8;

void printer_init();
void printer_set_density(uint8_t density);
printer_error_e printer_begin();
printer_error_e printer_poll();
printer_error_e printer_write_line(const uint8_t *data, uint32_t line_number);
printer_error_e printer_end(uint32_t feed_steps);
void printer_abort();
printer_error_e printer_feed_start();
// Call from the printer task at the configured feed interval.
printer_error_e printer_feed_step();
void printer_feed_stop();

#endif
