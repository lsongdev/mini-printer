#ifndef _TRANSPORT_COMMON_H_
#define _TRANSPORT_COMMON_H_

#include "protocol/print_protocol.h"

#include <stddef.h>
#include <stdint.h>

bool transport_submit_print_command(
    const print_protocol_command_t *command,
    uint32_t session,
    uint32_t timeout_ms);

size_t transport_encode_status(uint8_t *output, size_t output_size);

#endif
