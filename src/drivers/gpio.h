// gpio.h
#ifndef GPIO_H
#define GPIO_H

typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_SLOW,
    LED_FAST,
} LED_STATUS;

void gpio_init();
void gpio_led_on();
void gpio_led_off();
void gpio_led_set_status(LED_STATUS status);

void gpio_heater_init();
void gpio_heater_power_on();
void gpio_heater_power_off();

void gpio_button_init();
int gpio_paper_check();


#endif // GPIO_H
