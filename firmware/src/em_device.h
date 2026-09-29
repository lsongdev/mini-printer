#ifndef _EM_DEVICE_H_
#define _EM_DEVICE_H_

#include "em_config.h"

typedef enum {
    PRINTER_STATUS_IDLE = 0,
    PRINTER_STATUS_PRINTING,
    PRINTER_STATUS_FEEDING,
    PRINTER_STATUS_ERROR,
} printer_state_e;

typedef enum {
    PRINTER_ERROR_NONE = 0,
    PRINTER_ERROR_NO_PAPER,
    PRINTER_ERROR_OVERHEAT,
    PRINTER_ERROR_TEMPERATURE_SENSOR,
    PRINTER_ERROR_TIMEOUT,
    PRINTER_ERROR_CANCELLED,
    PRINTER_ERROR_PROTOCOL,
} printer_error_e;

typedef enum {
    PAPER_STATUS_NORMAL = 0,
    PAPER_STATUS_LACK,
} paper_state_e;

typedef struct {
    uint8_t battery;
    uint8_t temperature;
    paper_state_e paper_state;
    printer_state_e printer_state;
    printer_error_e printer_error;
} device_state_t;

void init_device_state();

device_state_t get_device_state();

void set_device_battery(uint8_t battery);
void set_device_temperature(uint8_t temperature);
void set_device_paper_status(paper_state_e status);
void set_device_printer_status(printer_state_e status);
void set_device_printer_error(printer_error_e error);

#endif
