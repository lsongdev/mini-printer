#include "em_motor.h"

#include "em_config.h"

namespace {
constexpr uint8_t PHASE_COUNT = 8;
constexpr uint8_t phases[PHASE_COUNT][4] = {
    {0, 1, 1, 0}, {0, 0, 1, 0}, {1, 0, 1, 0}, {1, 0, 0, 0},
    {1, 0, 0, 1}, {0, 0, 0, 1}, {0, 1, 0, 1}, {0, 1, 0, 0},
};
uint8_t next_phase = 0;

void write_phase(uint8_t phase)
{
    digitalWrite(PIN_MOTOR_AP, phases[phase][0]);
    digitalWrite(PIN_MOTOR_AM, phases[phase][1]);
    digitalWrite(PIN_MOTOR_BP, phases[phase][2]);
    digitalWrite(PIN_MOTOR_BM, phases[phase][3]);
}
} // namespace

void motor_init()
{
    // Set output latches low before enabling the pins to avoid a power-up step.
    motor_release();
    pinMode(PIN_MOTOR_AP, OUTPUT);
    pinMode(PIN_MOTOR_AM, OUTPUT);
    pinMode(PIN_MOTOR_BP, OUTPUT);
    pinMode(PIN_MOTOR_BM, OUTPUT);
    next_phase = 0;
}

void motor_step()
{
    write_phase(next_phase);
    next_phase = (next_phase + 1) % PHASE_COUNT;
}

void motor_feed_steps(uint32_t steps)
{
    for (uint32_t i = 0; i < steps; ++i)
    {
        motor_step();
        delayMicroseconds(MOTOR_WATI_TIME);
    }
}

void motor_release()
{
    digitalWrite(PIN_MOTOR_AP, LOW);
    digitalWrite(PIN_MOTOR_AM, LOW);
    digitalWrite(PIN_MOTOR_BP, LOW);
    digitalWrite(PIN_MOTOR_BM, LOW);
}
