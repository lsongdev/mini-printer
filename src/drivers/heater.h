#ifndef HEATER_H
#define HEATER_H

void heater_init();
void heater_run_stb(int id);
void heater_run_stbs();
void heater_stop_stbs();
void heater_power_on();
void heater_power_off();

#endif