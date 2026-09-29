#ifndef EM_MOTOR_H
#define EM_MOTOR_H

#include <stdint.h>

void motor_init();
// One half-step. The caller controls the interval between print steps.
void motor_step();
// Paper-only motion at the configured feed interval.
void motor_feed_steps(uint32_t steps);
void motor_release();

#endif
