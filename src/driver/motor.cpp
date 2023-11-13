#include <Arduino.h>

// Motor pin definitions
#define PIN_MOTOR_AP 23
#define PIN_MOTOR_BP 21
#define PIN_MOTOR_AN 22
#define PIN_MOTOR_BN 19

// Constants for motor control
#define N_POLES 4
#define STEP_NCYCLES 8
#define MOTOR_DELAY 2000 // Adjust as needed

// Motor control tables
static uint8_t motor_pin_table[N_POLES] = {
    PIN_MOTOR_AP, 
    PIN_MOTOR_BP, 
    PIN_MOTOR_AN, 
    PIN_MOTOR_BN
};

// Step table for motor
static uint8_t motor_step_table[STEP_NCYCLES][N_POLES] = {
    {1, 0, 0, 1}, // step 1 ↖️
    {0, 0, 0, 1}, // step 2 ⬅
    {0, 0, 1, 1}, // step 3 ↙️
    {0, 0, 1, 0}, // step 4 ⬇
    {0, 1, 1, 0}, // step 5 ↘️
    {0, 1, 0, 0}, // step 6 ➡
    {1, 1, 0, 0}, // step 7 ↗️
    {1, 0, 0, 0}, // step 8 ⬆
};

// Current position of the motor
static uint8_t motor_pos = 0;

void motor_init() {
    for (int i = 0; i < N_POLES; i++) {
        pinMode(motor_pin_table[i], OUTPUT);
        digitalWrite(motor_pin_table[i], LOW);
    }
}

void motor_step(bool forward) {
    for (int i = 0; i < N_POLES; i++) {
        digitalWrite(motor_pin_table[i], motor_step_table[motor_pos][i]);
    }
    motor_pos = motor_pos + (forward ? 1 : -1) % STEP_NCYCLES;
    delayMicroseconds(MOTOR_DELAY);
}

void motor_run(int steps, bool forward = true) {
    while (steps--) {
        motor_step(forward);
    }
}

void motor_stop() {
    for (int i = 0; i < N_POLES; i++) {
        digitalWrite(motor_pin_table[i], LOW);
    }
}
