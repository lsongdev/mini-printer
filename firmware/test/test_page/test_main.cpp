#include <unity.h>

#include "em_config.h"
#include "hal/em_printer.h"
#include "../../src/core/test_page.cpp"

namespace {
uint16_t lines = 0;
uint16_t nonempty_lines = 0;
uint32_t printed_dots = 0;
bool ended = false;

bool dot_is_set(const uint8_t *line, uint16_t x)
{
    return (line[x / 8] & (0x80u >> (x % 8))) != 0;
}
}

bool print_service_begin(uint32_t, uint8_t density, uint32_t)
{
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_PRINT_DENSITY, density);
    return true;
}

bool print_service_line(uint32_t, const uint8_t *line, uint32_t)
{
    ++lines;
    uint16_t dots = 0;
    for (uint8_t i = 0; i < TPH_DI_LEN; ++i)
        dots += __builtin_popcount(static_cast<unsigned>(line[i]));
    printed_dots += dots;
    if (dots != 0)
        ++nonempty_lines;
    return true;
}

bool print_service_end(uint32_t, uint32_t)
{
    ended = true;
    return true;
}

bool print_service_cancel(uint32_t, uint32_t)
{
    TEST_FAIL_MESSAGE("Unexpected test page cancellation");
    return false;
}

void setUp()
{
    lines = nonempty_lines = 0;
    printed_dots = 0;
    ended = false;
}
void tearDown() {}

void test_page_sends_visible_raster()
{
    TEST_ASSERT_TRUE(print_test_page());
    TEST_ASSERT_EQUAL_UINT16(170, lines);
    TEST_ASSERT_TRUE(ended);
    TEST_ASSERT_GREATER_THAN_UINT16(100, nonempty_lines);
    TEST_ASSERT_GREATER_THAN_UINT32(3000, printed_dots);
}

void test_every_mark_stays_inside_a_continuous_frame()
{
    uint8_t line[TPH_DI_LEN];
    for (uint16_t y = 0; y < 170; ++y)
    {
        render_line(y, line);
        for (uint16_t x = 0; x < TPH_DOTS_PER_LINE; ++x)
            if (x < FRAME_LEFT || x > FRAME_RIGHT)
                TEST_ASSERT_FALSE(dot_is_set(line, x));

        if (y >= 4 && y <= 165)
        {
            TEST_ASSERT_TRUE(dot_is_set(line, FRAME_LEFT));
            TEST_ASSERT_TRUE(dot_is_set(line, FRAME_RIGHT));
        }
    }
}

void test_each_strobe_group_gets_an_equal_width_stripe()
{
    uint8_t line[TPH_DI_LEN];
    for (uint8_t group = 0; group < 6; ++group)
    {
        render_line(8 + group * 5, line);
        const uint16_t start = group * 64 + 12;
        for (uint16_t x = start; x < start + 40; ++x)
            TEST_ASSERT_EQUAL_INT((x & 1) != 0, dot_is_set(line, x));
    }
}

void test_checkerboard_uses_complete_equal_tiles()
{
    uint8_t line[TPH_DI_LEN];
    for (uint16_t y = 136; y < 152; ++y)
    {
        render_line(y, line);
        for (uint16_t x = 16; x < 368; ++x)
        {
            const bool expected = (((x - 16) / 8 + (y - 136) / 8) & 1) == 0;
            TEST_ASSERT_EQUAL_INT(expected, dot_is_set(line, x));
        }
        for (uint16_t x = 12; x < 16; ++x)
            TEST_ASSERT_FALSE(dot_is_set(line, x));
        for (uint16_t x = 368; x <= 371; ++x)
            TEST_ASSERT_FALSE(dot_is_set(line, x));
    }
}

void test_bold_lines_stop_at_content_margin()
{
    uint8_t line[TPH_DI_LEN];
    for (uint16_t y = 80; y <= 81; ++y)
    {
        render_line(y, line);
        for (uint16_t x = CONTENT_LEFT; x <= CONTENT_RIGHT; ++x)
            TEST_ASSERT_TRUE(dot_is_set(line, x));
        TEST_ASSERT_FALSE(dot_is_set(line, FRAME_LEFT + 1));
        TEST_ASSERT_FALSE(dot_is_set(line, FRAME_RIGHT - 1));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_page_sends_visible_raster);
    RUN_TEST(test_every_mark_stays_inside_a_continuous_frame);
    RUN_TEST(test_each_strobe_group_gets_an_equal_width_stripe);
    RUN_TEST(test_checkerboard_uses_complete_equal_tiles);
    RUN_TEST(test_bold_lines_stop_at_content_margin);
    return UNITY_END();
}
