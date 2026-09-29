#include <unity.h>

#include "hal/em_button.h"
#include "em_config.h"

namespace {
uint8_t &level = mock_gpio_level(PIN_KEY);
int events[BUTTON_EVENT_COUNT] = {};

void count_event(void *button)
{
    ++events[Get_Button_Event(static_cast<Button_t *>(button))];
}

void scan(int ticks)
{
    for (int i = 0; i < ticks; ++i)
        Button_Process();
}

void setup_button(Button_t &button)
{
    level = 1;
    for (int &count : events)
        count = 0;
    Button_Create(&button, PIN_KEY, LOW);
    for (int event = 0; event < BUTTON_EVENT_COUNT; ++event)
        Button_Attach(&button, static_cast<Button_Event>(event), count_event);
}

void test_single_click_waits_for_double_window()
{
    Button_t button;
    setup_button(button);
    level = 0;
    scan(2);
    level = 1;
    scan(2);
    scan(14);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_SINGLE]);
    scan(1);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_SINGLE]);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_DOUBLE]);
    Button_Delete(&button);
}

void test_double_click_emits_once()
{
    Button_t button;
    setup_button(button);
    level = 0;
    scan(2);
    level = 1;
    scan(2);
    scan(5);
    level = 0;
    scan(2);
    level = 1;
    scan(2);
    scan(20);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_SINGLE]);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_DOUBLE]);
    Button_Delete(&button);
}

void test_long_press_starts_once_and_stops_on_release()
{
    Button_t button;
    setup_button(button);
    level = 0;
    scan(2);
    scan(50);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_LONG]);
    scan(100);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_LONG]);
    level = 1;
    scan(2);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_LONG_RELEASE]);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_SINGLE]);
    Button_Delete(&button);
}

void test_bounce_does_not_start_press()
{
    Button_t button;
    setup_button(button);
    level = 0;
    scan(1);
    level = 1;
    scan(1);
    scan(60);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_LONG]);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_SINGLE]);
    Button_Delete(&button);
}

void test_second_press_can_become_long_press()
{
    Button_t button;
    setup_button(button);
    level = 0;
    scan(2);
    level = 1;
    scan(2);
    level = 0;
    scan(2);
    scan(50);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_SINGLE]);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_DOUBLE]);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_LONG]);
    level = 1;
    scan(2);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_LONG_RELEASE]);
    Button_Delete(&button);
}

void test_release_bounce_does_not_turn_short_press_into_long_press()
{
    Button_t button;
    setup_button(button);
    level = 0;
    scan(2);
    scan(49);
    level = 1;
    scan(2);
    scan(15);
    TEST_ASSERT_EQUAL_INT(0, events[BUTTON_LONG]);
    TEST_ASSERT_EQUAL_INT(1, events[BUTTON_SINGLE]);
    Button_Delete(&button);
}
} // namespace

void setUp() {}
void tearDown() {}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_click_waits_for_double_window);
    RUN_TEST(test_double_click_emits_once);
    RUN_TEST(test_long_press_starts_once_and_stops_on_release);
    RUN_TEST(test_bounce_does_not_start_press);
    RUN_TEST(test_second_press_can_become_long_press);
    RUN_TEST(test_release_bounce_does_not_turn_short_press_into_long_press);
    return UNITY_END();
}
