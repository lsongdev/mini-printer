#include "em_device.h"

namespace {
device_state_t device_state;
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
}

void init_device_state()
{
    portENTER_CRITICAL(&state_mux);
    memset(&device_state, 0, sizeof(device_state));
    device_state.battery = 100;
    device_state.temperature = 30;
    device_state.paper_state = PAPER_STATUS_NORMAL;
    device_state.printer_state = PRINTER_STATUS_IDLE;
    device_state.printer_error = PRINTER_ERROR_NONE;
    portEXIT_CRITICAL(&state_mux);
}

device_state_t get_device_state()
{
    device_state_t snapshot;

    portENTER_CRITICAL(&state_mux);
    snapshot = device_state;
    portEXIT_CRITICAL(&state_mux);

    return snapshot;
}

void set_device_battery(uint8_t battery)
{
    portENTER_CRITICAL(&state_mux);
    device_state.battery = battery;
    portEXIT_CRITICAL(&state_mux);
}

void set_device_temperature(uint8_t temperature)
{
    portENTER_CRITICAL(&state_mux);
    device_state.temperature = temperature;
    portEXIT_CRITICAL(&state_mux);
}

void set_device_paper_status(paper_state_e status)
{
    portENTER_CRITICAL(&state_mux);
    device_state.paper_state = status;
    portEXIT_CRITICAL(&state_mux);
}

void set_device_printer_status(printer_state_e status)
{
    portENTER_CRITICAL(&state_mux);
    device_state.printer_state = status;
    portEXIT_CRITICAL(&state_mux);
}

void set_device_printer_error(printer_error_e error)
{
    portENTER_CRITICAL(&state_mux);
    device_state.printer_error = error;
    portEXIT_CRITICAL(&state_mux);
}
