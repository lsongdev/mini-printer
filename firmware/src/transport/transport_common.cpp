#include "transport_common.h"

#include "core/print_service.h"
#include "em_device.h"

bool transport_submit_print_command(
    const print_protocol_command_t *command,
    uint32_t session,
    uint32_t timeout_ms)
{
    if (command == nullptr)
        return false;

    switch (command->type)
    {
    case PRINT_PROTOCOL_BEGIN:
        return print_service_begin(session, command->density, timeout_ms);

    case PRINT_PROTOCOL_LINE:
        return print_service_line(session, command->line, timeout_ms);

    case PRINT_PROTOCOL_END:
        return print_service_end(session, timeout_ms);

    case PRINT_PROTOCOL_CANCEL:
        return print_service_cancel(session, timeout_ms);

    default:
        return false;
    }
}

size_t transport_encode_status(uint8_t *output, size_t output_size)
{
    const device_state_t state = get_device_state();

    const uint8_t payload[] = {
        state.battery,
        state.temperature,
        static_cast<uint8_t>(state.paper_state),
        static_cast<uint8_t>(state.printer_state),
        static_cast<uint8_t>(state.printer_error),
    };

    return print_protocol_encode(
        PRINT_PROTOCOL_STATUS,
        payload,
        sizeof(payload),
        output,
        output_size);
}
