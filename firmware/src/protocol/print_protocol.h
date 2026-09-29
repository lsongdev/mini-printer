#ifndef _PRINT_PROTOCOL_H_
#define _PRINT_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#define PRINT_PROTOCOL_VERSION 1
#define PRINT_PROTOCOL_HEADER_SIZE 6
#define PRINT_PROTOCOL_MAX_PAYLOAD 48

typedef enum {
    PRINT_PROTOCOL_ERROR_NONE = 0,
    PRINT_PROTOCOL_ERROR_INVALID_FRAME,
    PRINT_PROTOCOL_ERROR_REJECTED,
} print_protocol_error_e;

typedef enum {
    PRINT_PROTOCOL_BEGIN = 0x01,
    PRINT_PROTOCOL_LINE = 0x02,
    PRINT_PROTOCOL_END = 0x03,
    PRINT_PROTOCOL_CANCEL = 0x04,
    PRINT_PROTOCOL_STATUS_REQUEST = 0x10,
    PRINT_PROTOCOL_STATUS = 0x80,
} print_protocol_type_e;

typedef struct {
    print_protocol_type_e type;
    uint8_t density;
    uint8_t line[PRINT_PROTOCOL_MAX_PAYLOAD];
} print_protocol_command_t;

typedef bool (*print_protocol_handler_t)(
    const print_protocol_command_t *command,
    void *context);

typedef struct {
    print_protocol_handler_t handler;
    void *context;

    uint8_t header[PRINT_PROTOCOL_HEADER_SIZE];
    size_t header_pos;

    uint8_t payload[PRINT_PROTOCOL_MAX_PAYLOAD];
    uint16_t payload_len;
    uint16_t payload_pos;
    print_protocol_error_e error;
} print_protocol_parser_t;

void print_protocol_init(
    print_protocol_parser_t *parser,
    print_protocol_handler_t handler,
    void *context);

void print_protocol_reset(print_protocol_parser_t *parser);

print_protocol_error_e print_protocol_error(
    const print_protocol_parser_t *parser);

bool print_protocol_feed(
    print_protocol_parser_t *parser,
    const uint8_t *data,
    size_t length);

size_t print_protocol_encode(
    print_protocol_type_e type,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *output,
    size_t output_size);

#endif
