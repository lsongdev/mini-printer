#include <unity.h>

#include "protocol/print_protocol.h"

#include <string.h>

namespace {
struct capture_t {
    int count;
    print_protocol_command_t last;
};

bool capture_command(const print_protocol_command_t *command, void *context)
{
    capture_t *capture = static_cast<capture_t *>(context);
    capture->last = *command;
    capture->count++;
    return true;
}

bool reject_command(const print_protocol_command_t *, void *)
{
    return false;
}

void test_fragmented_begin()
{
    capture_t capture = {};
    print_protocol_parser_t parser;
    print_protocol_init(&parser, capture_command, &capture);

    uint8_t frame[16];
    const uint8_t density = 72;
    const size_t length = print_protocol_encode(
        PRINT_PROTOCOL_BEGIN,
        &density,
        1,
        frame,
        sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(7, length);

    for (size_t i = 0; i < length; ++i)
        TEST_ASSERT_TRUE(print_protocol_feed(&parser, &frame[i], 1));

    TEST_ASSERT_EQUAL_INT(1, capture.count);
    TEST_ASSERT_EQUAL_INT(PRINT_PROTOCOL_BEGIN, capture.last.type);
    TEST_ASSERT_EQUAL_UINT8(72, capture.last.density);
}

void test_fragmented_line()
{
    capture_t capture = {};
    print_protocol_parser_t parser;
    print_protocol_init(&parser, capture_command, &capture);

    uint8_t line[48];
    for (size_t i = 0; i < sizeof(line); ++i)
        line[i] = static_cast<uint8_t>(i);

    uint8_t frame[64];
    const size_t length = print_protocol_encode(
        PRINT_PROTOCOL_LINE,
        line,
        sizeof(line),
        frame,
        sizeof(frame));

    TEST_ASSERT_EQUAL_UINT32(54, length);
    TEST_ASSERT_TRUE(print_protocol_feed(&parser, frame, 3));
    TEST_ASSERT_TRUE(print_protocol_feed(&parser, frame + 3, 8));
    TEST_ASSERT_TRUE(print_protocol_feed(&parser, frame + 11, length - 11));

    TEST_ASSERT_EQUAL_INT(1, capture.count);
    TEST_ASSERT_EQUAL_INT(PRINT_PROTOCOL_LINE, capture.last.type);
    TEST_ASSERT_EQUAL_MEMORY(line, capture.last.line, sizeof(line));
}

void test_concatenated_frames()
{
    capture_t capture = {};
    print_protocol_parser_t parser;
    print_protocol_init(&parser, capture_command, &capture);

    uint8_t stream[32];
    size_t offset = 0;

    const uint8_t density = 64;
    offset += print_protocol_encode(
        PRINT_PROTOCOL_BEGIN,
        &density,
        1,
        stream + offset,
        sizeof(stream) - offset);

    offset += print_protocol_encode(
        PRINT_PROTOCOL_END,
        nullptr,
        0,
        stream + offset,
        sizeof(stream) - offset);

    TEST_ASSERT_TRUE(print_protocol_feed(&parser, stream, offset));
    TEST_ASSERT_EQUAL_INT(2, capture.count);
    TEST_ASSERT_EQUAL_INT(PRINT_PROTOCOL_END, capture.last.type);
}

void test_invalid_length_rejected()
{
    capture_t capture = {};
    print_protocol_parser_t parser;
    print_protocol_init(&parser, capture_command, &capture);

    const uint8_t invalid[] = {
        'M', 'P', PRINT_PROTOCOL_VERSION,
        PRINT_PROTOCOL_LINE,
        47, 0,
    };

    TEST_ASSERT_FALSE(print_protocol_feed(
        &parser,
        invalid,
        sizeof(invalid)));

    TEST_ASSERT_EQUAL_INT(0, capture.count);
    TEST_ASSERT_EQUAL_INT(
        PRINT_PROTOCOL_ERROR_INVALID_FRAME,
        print_protocol_error(&parser));
}

void test_handler_rejection_is_distinct()
{
    print_protocol_parser_t parser;
    print_protocol_init(&parser, reject_command, nullptr);

    uint8_t frame[PRINT_PROTOCOL_HEADER_SIZE];
    const size_t length = print_protocol_encode(
        PRINT_PROTOCOL_END,
        nullptr,
        0,
        frame,
        sizeof(frame));

    TEST_ASSERT_FALSE(print_protocol_feed(&parser, frame, length));
    TEST_ASSERT_EQUAL_INT(
        PRINT_PROTOCOL_ERROR_REJECTED,
        print_protocol_error(&parser));
}

void test_status_request()
{
    capture_t capture = {};
    print_protocol_parser_t parser;
    print_protocol_init(&parser, capture_command, &capture);

    uint8_t frame[PRINT_PROTOCOL_HEADER_SIZE];
    const size_t length = print_protocol_encode(
        PRINT_PROTOCOL_STATUS_REQUEST,
        nullptr,
        0,
        frame,
        sizeof(frame));

    TEST_ASSERT_TRUE(print_protocol_feed(&parser, frame, length));
    TEST_ASSERT_EQUAL_INT(1, capture.count);
    TEST_ASSERT_EQUAL_INT(PRINT_PROTOCOL_STATUS_REQUEST, capture.last.type);
}
} // namespace

void setUp() {}
void tearDown() {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_fragmented_begin);
    RUN_TEST(test_fragmented_line);
    RUN_TEST(test_concatenated_frames);
    RUN_TEST(test_invalid_length_rejected);
    RUN_TEST(test_handler_rejection_is_distinct);
    RUN_TEST(test_status_request);

    return UNITY_END();
}
