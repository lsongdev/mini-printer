#include "em_indicator.h"

#include "em_config.h"

namespace {
#ifdef BEEP_MODE
constexpr int BEEP_CHANNEL = 0;
constexpr int BEEP_FREQUENCY = 2000;
constexpr int BEEP_RESOLUTION = 8;
#endif
}

void indicator_init()
{
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
#ifdef BEEP_MODE
    // The V4 pin map shares this pin with the LED. Enable BEEP_MODE only on
    // a board where the buzzer has a dedicated pin.
    static_assert(PIN_BEEP != PIN_LED, "Buzzer and LED must use separate pins");
    ledcSetup(BEEP_CHANNEL, BEEP_FREQUENCY, BEEP_RESOLUTION);
    ledcAttachPin(PIN_BEEP, BEEP_CHANNEL);
#endif
}

void indicator_beep(indicator_pattern_e pattern)
{
#ifdef BEEP_MODE
    const uint8_t count = pattern == INDICATOR_WARN ? 3 : 1;
    const note_t note = pattern == INDICATOR_CONNECT ? NOTE_F : NOTE_D;
    for (uint8_t i = 0; i < count; ++i)
    {
        ledcWriteNote(BEEP_CHANNEL, note, 5);
        delay(100);
        ledcWrite(BEEP_CHANNEL, 0);
        if (i + 1 < count)
            delay(100);
    }
#else
    (void)pattern;
#endif
}

void indicator_led(indicator_pattern_e pattern)
{
    const uint8_t count = pattern == INDICATOR_WARN ? 3 : 1;
    for (uint8_t i = 0; i < count; ++i)
    {
        digitalWrite(PIN_LED, LOW);
        delay(100);
        digitalWrite(PIN_LED, HIGH);
        if (i + 1 < count)
            delay(100);
    }
}
