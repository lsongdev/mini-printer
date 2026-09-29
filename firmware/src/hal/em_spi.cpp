#include "em_spi.h"

#include "em_config.h"
#include "em_printer.h"

#include <SPI.h>
#include <string.h>

namespace {
constexpr uint32_t SPI_CLOCK_HZ = 1000000;
static_assert(PIN_SPI_MISO != PIN_LAT, "SPI MISO must not take over the print-head latch pin");
SPIClass printer_spi(HSPI);
const SPISettings settings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0);
bool initialized = false;
} // namespace

void printer_spi_init()
{
    // On ESP32, MISO=-1 falls back to GPIO12. GPIO12 is LAT on this board,
    // and SPIClass::begin would reconfigure the latch as an input.
    printer_spi.begin(PIN_SCK, PIN_SPI_MISO, PIN_SDA, -1);
    initialized = true;
}

bool printer_spi_write(const uint8_t *data, size_t length)
{
    if (!initialized || data == nullptr || length == 0)
        return false;

    // SPIClass::transfer uses the same full-duplex path as the board firmware
    // and overwrites its buffer. Keep the public interface read-only.
    uint8_t line[TPH_DI_LEN];
    if (length != sizeof(line))
        return false;
    memcpy(line, data, length);
    printer_spi.beginTransaction(settings);
    printer_spi.transfer(line, length);
    printer_spi.endTransaction();
    return true;
}
