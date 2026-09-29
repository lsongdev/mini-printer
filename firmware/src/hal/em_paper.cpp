#include "em_paper.h"

#include "em_config.h"

namespace {
volatile bool changed = false;
portMUX_TYPE change_mux = portMUX_INITIALIZER_UNLOCKED;

void ARDUINO_ISR_ATTR paper_isr()
{
    portENTER_CRITICAL_ISR(&change_mux);
    changed = true;
    portEXIT_CRITICAL_ISR(&change_mux);
}
} // namespace

void paper_init()
{
    pinMode(PIN_PAPER, INPUT);
    changed = false;
    attachInterrupt(digitalPinToInterrupt(PIN_PAPER), paper_isr, CHANGE);
}

bool paper_present()
{
    return digitalRead(PIN_PAPER) == LOW;
}

bool paper_take_change()
{
    portENTER_CRITICAL(&change_mux);
    const bool result = changed;
    changed = false;
    portEXIT_CRITICAL(&change_mux);
    return result;
}
