#include "test_page.h"

#include "print_service.h"
#include "em_config.h"
#include "hal/em_printer.h"

#include <string.h>

namespace {
constexpr uint32_t TEST_PAGE_SESSION = 0x7FFFFFFFu;
constexpr uint32_t QUEUE_TIMEOUT_MS = 1000;
constexpr uint16_t PAGE_LINES = 170;
constexpr uint16_t FRAME_LEFT = 8;
constexpr uint16_t FRAME_RIGHT = TPH_DOTS_PER_LINE - 9;
constexpr uint16_t CONTENT_LEFT = 12;
constexpr uint16_t CONTENT_RIGHT = TPH_DOTS_PER_LINE - 13;
static_assert(TPH_DOTS_PER_LINE == TPH_DI_LEN * 8, "Raster width must match print head");

void set_dot(uint8_t line[TPH_DI_LEN], uint16_t x)
{
    line[x / 8] |= static_cast<uint8_t>(0x80u >> (x % 8));
}

const uint8_t *glyph(char character)
{
    static const uint8_t t[7] = {31, 4, 4, 4, 4, 4, 4};
    static const uint8_t e[7] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t s[7] = {15, 16, 16, 14, 1, 1, 30};
    static const uint8_t p[7] = {30, 17, 17, 30, 16, 16, 16};
    static const uint8_t a[7] = {14, 17, 17, 31, 17, 17, 17};
    static const uint8_t g[7] = {15, 16, 16, 19, 17, 17, 15};

    switch (character)
    {
    case 'T': return t;
    case 'E': return e;
    case 'S': return s;
    case 'P': return p;
    case 'A': return a;
    case 'G': return g;
    default: return nullptr;
    }
}

void render_line(uint16_t y, uint8_t line[TPH_DI_LEN])
{
    memset(line, 0, TPH_DI_LEN);

    if (y == 4 || y == PAGE_LINES - 5)
    {
        for (uint16_t x = FRAME_LEFT; x <= FRAME_RIGHT; ++x)
            set_dot(line, x);
        return;
    }

    if (y > 4 && y < PAGE_LINES - 5)
    {
        set_dot(line, FRAME_LEFT);
        set_dot(line, FRAME_RIGHT);
    }

    // Match the hardware guide's 0x55 per-channel test: five raster lines
    // exercise each 64-dot strobe group at half load. The 40-dot bars have
    // equal width and stay inside the frame, including the edge groups.
    if (y >= 8 && y < 38)
    {
        const uint8_t group = (y - 8) / 5;
        const uint16_t start = group * 64 + 12;
        for (uint16_t x = start; x < start + 40; ++x)
            if (x & 1)
                set_dot(line, x);
        return;
    }

    if (y >= 46 && y < 67)
    {
        constexpr char TITLE[] = "TEST PAGE";
        const uint8_t row = (y - 46) / 3;
        for (uint8_t index = 0; index < sizeof(TITLE) - 1; ++index)
        {
            const uint8_t *font = glyph(TITLE[index]);
            if (font == nullptr)
                continue;

            for (uint8_t column = 0; column < 5; ++column)
            {
                if ((font[row] & (0x10u >> column)) == 0)
                    continue;
                for (uint8_t scale = 0; scale < 3; ++scale)
                    set_dot(line, 111 + index * 18 + column * 3 + scale);
            }
        }
    }

    // One low-current marker in each 64-dot STB group. These help distinguish
    // missing strobe groups from a supply that droops under dense printing.
    if (y >= 70 && y < 76)
    {
        for (uint8_t group = 0; group < 6; ++group)
            set_dot(line, 32 + group * 64);
    }

    // Two solid rows inside the frame, after the title and sparse markers.
    if (y == 80 || y == 81)
    {
        for (uint16_t x = CONTENT_LEFT; x <= CONTENT_RIGHT; ++x)
            set_dot(line, x);
        return;
    }

    // Six 60-dot bands exercise the full 384-dot head at increasing density.
    if (y >= 86 && y < 134)
    {
        for (uint16_t x = CONTENT_LEFT; x <= CONTENT_RIGHT; ++x)
        {
            const uint8_t band = (x - 12) / 60;
            if ((x - 12) % 60 >= 57)
                continue;
            if (((x + y * 3) & 7) < band + 1)
                set_dot(line, x);
        }
    }

    // Align both axes to complete 8-by-8 checker tiles.
    if (y >= 136 && y < 152)
    {
        for (uint16_t x = 16; x < 368; ++x)
        {
            if ((((x - 16) / 8) + ((y - 136) / 8)) % 2 == 0)
                set_dot(line, x);
        }
    }

}
} // namespace

bool print_test_page()
{
    if (!print_service_begin(TEST_PAGE_SESSION, DEFAULT_PRINT_DENSITY, QUEUE_TIMEOUT_MS))
        return false;

    uint8_t line[TPH_DI_LEN];
    for (uint16_t y = 0; y < PAGE_LINES; ++y)
    {
        render_line(y, line);
        if (!print_service_line(TEST_PAGE_SESSION, line, QUEUE_TIMEOUT_MS))
        {
            print_service_cancel(TEST_PAGE_SESSION, QUEUE_TIMEOUT_MS);
            return false;
        }
    }

    if (print_service_end(TEST_PAGE_SESSION, QUEUE_TIMEOUT_MS))
        return true;

    print_service_cancel(TEST_PAGE_SESSION, QUEUE_TIMEOUT_MS);
    return false;
}
