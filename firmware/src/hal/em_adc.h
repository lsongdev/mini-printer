#ifndef EM_ADC_H
#define EM_ADC_H

void adc_init();
// False leaves the output unchanged. Millivolts are measured at the ADC pin.
bool adc_read_battery_mv(int *millivolts);
bool adc_read_temperature_c(float *temperature);

#endif
