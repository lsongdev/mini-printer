#ifndef EM_SPI_H
#define EM_SPI_H

#include <stddef.h>
#include <stdint.h>

void printer_spi_init();
// Shifts exactly length bytes into the print head while its latch is inactive.
bool printer_spi_write(const uint8_t *data, size_t length);

#endif
