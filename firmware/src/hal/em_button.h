#ifndef EM_BUTTON_H
#define EM_BUTTON_H

#include <stdint.h>

// Button_Process is called every 20 ms by the button task.
enum Button_Event : uint8_t {
    BUTTON_SINGLE = 0,
    BUTTON_DOUBLE,
    BUTTON_LONG,
    BUTTON_LONG_RELEASE,
    BUTTON_EVENT_COUNT,
};

typedef void (*Button_CallBack)(void *button);

struct Button_t {
    uint8_t pin;
    uint8_t active_level;
    uint8_t stable_level;
    uint8_t candidate_level;
    uint8_t debounce_ticks;
    uint8_t phase;
    uint8_t hold_ticks;
    uint8_t double_ticks;
    Button_Event last_event;
    Button_CallBack callbacks[BUTTON_EVENT_COUNT];
    Button_t *next;
};

void Button_Create(Button_t *button, uint8_t pin, uint8_t active_level);
void Button_Attach(Button_t *button, Button_Event event, Button_CallBack callback);
void Button_Delete(Button_t *button);
void Button_Cycle_Process(Button_t *button);
void Button_Process();
Button_Event Get_Button_Event(const Button_t *button);

#endif
