#include "em_button.h"
#include "em_config.h"

#include <string.h>

namespace {
constexpr uint8_t DEBOUNCE_TICKS = 2;   // 40 ms
constexpr uint8_t DOUBLE_TICKS = 15;    // 300 ms after the first release
constexpr uint8_t LONG_TICKS = 50;      // 1 s

enum Phase : uint8_t {
    IDLE,
    FIRST_PRESS,
    WAIT_SECOND_PRESS,
    SECOND_PRESS,
    LONG_PRESS,
};

Button_t *head = nullptr;

void emit(Button_t *button, Button_Event event)
{
    button->last_event = event;
    if (button->callbacks[event] != nullptr)
        button->callbacks[event](button);
}

bool stable_level_changed(Button_t *button)
{
    const uint8_t level = digitalRead(button->pin);
    if (level == button->stable_level)
    {
        button->debounce_ticks = 0;
        return false;
    }

    if (level != button->candidate_level)
    {
        button->candidate_level = level;
        button->debounce_ticks = 1;
    }
    else if (++button->debounce_ticks >= DEBOUNCE_TICKS)
    {
        button->stable_level = level;
        button->debounce_ticks = 0;
        return true;
    }
    return false;
}

void begin_long_press(Button_t *button)
{
    // A second press that becomes a long press is not a double click.
    if (button->phase == SECOND_PRESS)
        emit(button, BUTTON_SINGLE);
    button->phase = LONG_PRESS;
    emit(button, BUTTON_LONG);
}
} // namespace

void Button_Create(Button_t *button, uint8_t pin, uint8_t active_level)
{
    if (button == nullptr)
        return;

    Button_Delete(button);
    memset(button, 0, sizeof(*button));
    button->pin = pin;
    button->active_level = active_level;
    pinMode(pin, active_level == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    button->stable_level = digitalRead(pin);
    button->candidate_level = button->stable_level;
    button->phase = IDLE;
    button->last_event = BUTTON_EVENT_COUNT;
    button->next = head;
    head = button;
}

void Button_Attach(Button_t *button, Button_Event event, Button_CallBack callback)
{
    if (button != nullptr && event < BUTTON_EVENT_COUNT)
        button->callbacks[event] = callback;
}

void Button_Delete(Button_t *button)
{
    for (Button_t **current = &head; *current != nullptr; current = &(*current)->next)
    {
        if (*current == button)
        {
            *current = button->next;
            button->next = nullptr;
            return;
        }
    }
}

void Button_Cycle_Process(Button_t *button)
{
    if (button == nullptr)
        return;

    const bool changed = stable_level_changed(button);
    const bool pressed = button->stable_level == button->active_level;

    if (changed)
    {
        if (pressed)
        {
            if (button->phase == WAIT_SECOND_PRESS)
            {
                button->phase = SECOND_PRESS;
                button->hold_ticks = 0;
            }
            else if (button->phase == IDLE)
            {
                button->phase = FIRST_PRESS;
                button->hold_ticks = 0;
            }
        }
        else if (button->phase == FIRST_PRESS)
        {
            button->phase = WAIT_SECOND_PRESS;
            button->double_ticks = 0;
        }
        else if (button->phase == SECOND_PRESS)
        {
            button->phase = IDLE;
            emit(button, BUTTON_DOUBLE);
        }
        else if (button->phase == LONG_PRESS)
        {
            button->phase = IDLE;
            emit(button, BUTTON_LONG_RELEASE);
        }
        return;
    }

    // Do not count time while an opposite level is being debounced.
    if (button->debounce_ticks != 0)
        return;

    if (pressed && (button->phase == FIRST_PRESS || button->phase == SECOND_PRESS))
    {
        if (++button->hold_ticks >= LONG_TICKS)
            begin_long_press(button);
    }
    else if (!pressed && button->phase == WAIT_SECOND_PRESS)
    {
        if (++button->double_ticks >= DOUBLE_TICKS)
        {
            button->phase = IDLE;
            emit(button, BUTTON_SINGLE);
        }
    }
}

void Button_Process()
{
    for (Button_t *button = head; button != nullptr; )
    {
        Button_t *next = button->next;
        Button_Cycle_Process(button);
        button = next;
    }
}

Button_Event Get_Button_Event(const Button_t *button)
{
    return button == nullptr ? BUTTON_EVENT_COUNT : button->last_event;
}
