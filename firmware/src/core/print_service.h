#ifndef _PRINT_SERVICE_H_
#define _PRINT_SERVICE_H_

#include "em_device.h"

#include <Arduino.h>

bool print_service_init();

bool print_service_begin(uint32_t session, uint8_t density, uint32_t timeout_ms);
bool print_service_line(uint32_t session, const uint8_t *line, uint32_t timeout_ms);
bool print_service_end(uint32_t session, uint32_t timeout_ms);
bool print_service_cancel(uint32_t session, uint32_t timeout_ms);
bool print_service_abort(uint32_t session, printer_error_e error, uint32_t timeout_ms);

bool print_service_feed_start(uint32_t timeout_ms);
bool print_service_feed_stop(uint32_t timeout_ms);

#endif
