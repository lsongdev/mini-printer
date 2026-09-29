#include "print_protocol.h"

#include <string.h>

namespace {
constexpr uint8_t MAGIC_0 = 'M';
constexpr uint8_t MAGIC_1 = 'P';

void reset_frame(print_protocol_parser_t *parser)
{
    parser->header_pos = 0;
    parser->payload_len = 0;
    parser->payload_pos = 0;
}

int expected_payload_length(uint8_t type)
{
    switch (type)
    {
    case PRINT_PROTOCOL_BEGIN:
        return 1;
    case PRINT_PROTOCOL_LINE:
        return 48;
    case PRINT_PROTOCOL_END:
    case PRINT_PROTOCOL_CANCEL:
    case PRINT_PROTOCOL_STATUS_REQUEST:
        return 0;
    default:
        return -1;
    }
}

bool emit_command(print_protocol_parser_t *parser)
{
    print_protocol_command_t command = {};
    command.type =
        static_cast<print_protocol_type_e>(parser->header[3]);

    if (command.type == PRINT_PROTOCOL_BEGIN)
        command.density = parser->payload[0];
    else if (command.type == PRINT_PROTOCOL_LINE)
        memcpy(command.line, parser->payload, sizeof(command.line));

    const bool accepted =
        parser->handler != nullptr &&
        parser->handler(&command, parser->context);

    if (!accepted)
        parser->error = PRINT_PROTOCOL_ERROR_REJECTED;

    reset_frame(parser);
    return accepted;
}

bool finish_header(print_protocol_parser_t *parser)
{
    if (parser->header[0] != MAGIC_0 ||
        parser->header[1] != MAGIC_1 ||
        parser->header[2] != PRINT_PROTOCOL_VERSION)
    {
        parser->error = PRINT_PROTOCOL_ERROR_INVALID_FRAME;
        reset_frame(parser);
        return false;
    }

    const int expected = expected_payload_length(parser->header[3]);
    if (expected < 0)
    {
        parser->error = PRINT_PROTOCOL_ERROR_INVALID_FRAME;
        reset_frame(parser);
        return false;
    }

    const uint16_t payload_len =
        static_cast<uint16_t>(parser->header[4]) |
        (static_cast<uint16_t>(parser->header[5]) << 8);

    if (payload_len != expected ||
        payload_len > PRINT_PROTOCOL_MAX_PAYLOAD)
    {
        parser->error = PRINT_PROTOCOL_ERROR_INVALID_FRAME;
        reset_frame(parser);
        return false;
    }

    parser->payload_len = payload_len;
    parser->payload_pos = 0;

    if (payload_len == 0)
        return emit_command(parser);

    return true;
}
} // namespace

void print_protocol_init(
    print_protocol_parser_t *parser,
    print_protocol_handler_t handler,
    void *context)
{
    if (parser == nullptr)
        return;

    memset(parser, 0, sizeof(*parser));
    parser->handler = handler;
    parser->context = context;
}

void print_protocol_reset(print_protocol_parser_t *parser)
{
    if (parser == nullptr)
        return;

    print_protocol_handler_t handler = parser->handler;
    void *context = parser->context;

    memset(parser, 0, sizeof(*parser));
    parser->handler = handler;
    parser->context = context;
}

print_protocol_error_e print_protocol_error(
    const print_protocol_parser_t *parser)
{
    return parser == nullptr
               ? PRINT_PROTOCOL_ERROR_INVALID_FRAME
               : parser->error;
}

bool print_protocol_feed(
    print_protocol_parser_t *parser,
    const uint8_t *data,
    size_t length)
{
    if (parser == nullptr || data == nullptr)
        return false;

    parser->error = PRINT_PROTOCOL_ERROR_NONE;

    for (size_t index = 0; index < length; ++index)
    {
        if (parser->header_pos < PRINT_PROTOCOL_HEADER_SIZE)
        {
            parser->header[parser->header_pos++] = data[index];

            if (parser->header_pos == PRINT_PROTOCOL_HEADER_SIZE &&
                !finish_header(parser))
                return false;

            continue;
        }

        parser->payload[parser->payload_pos++] = data[index];

        if (parser->payload_pos == parser->payload_len &&
            !emit_command(parser))
            return false;
    }

    return true;
}

size_t print_protocol_encode(
    print_protocol_type_e type,
    const uint8_t *payload,
    uint16_t payload_len,
    uint8_t *output,
    size_t output_size)
{
    const size_t total = PRINT_PROTOCOL_HEADER_SIZE + payload_len;

    if (output == nullptr ||
        output_size < total ||
        payload_len > PRINT_PROTOCOL_MAX_PAYLOAD ||
        (payload_len > 0 && payload == nullptr))
        return 0;

    output[0] = MAGIC_0;
    output[1] = MAGIC_1;
    output[2] = PRINT_PROTOCOL_VERSION;
    output[3] = static_cast<uint8_t>(type);
    output[4] = static_cast<uint8_t>(payload_len & 0xFF);
    output[5] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);

    if (payload_len > 0)
        memcpy(output + PRINT_PROTOCOL_HEADER_SIZE, payload, payload_len);

    return total;
}
