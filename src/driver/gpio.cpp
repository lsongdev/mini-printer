#include <Arduino.h>
#include "gpio.h"

// 打印头电源升压控制引脚
#define PIN_VHEN 17
#define PIN_LED 18
#define PIN_BTN 5
// 缺纸检测引脚
#define PIN_PAPER 35

void gpio_led_on()
{
    digitalWrite(PIN_LED, HIGH);
}

void gpio_led_off()
{
    digitalWrite(PIN_LED, LOW);
}

void gpio_led_blink(int ms)
{
    gpio_led_off();
    delay(ms);
    gpio_led_on();
    delay(ms);
}

void gpio_led_set_status(LED_STATUS status)
{
    switch (status)
    {
    case LED_OFF:
        gpio_led_off();
        break;
    case LED_ON:
        gpio_led_on();
        break;
    case LED_SLOW:
        gpio_led_blink(1000);
        break;
    case LED_FAST:
        gpio_led_blink(200);
        break;
    default:
        break;
    }
}

void gpio_led_init()
{
    pinMode(PIN_LED, OUTPUT);
    gpio_led_off();
}

void gpio_heater_power_on()
{
    digitalWrite(PIN_VHEN, HIGH);
}

void gpio_heater_power_off()
{
    digitalWrite(PIN_VHEN, LOW);
}

void gpio_heater_init()
{
    pinMode(PIN_VHEN, OUTPUT);
    gpio_heater_power_off();
}

void ARDUINO_ISR_ATTR gpio_button_isr()
{
    // 按钮中断服务程序
    Serial.println("Button Click!");
}

void gpio_button_init()
{
    attachInterrupt(PIN_BTN, gpio_button_isr, FALLING);
}

void ARDUINO_ISR_ATTR gpio_paper_isr()
{
    // 缺纸检测中断服务程序
    Serial.println("No Paper!");
}

void gpio_paper_init()
{
    pinMode(PIN_PAPER, INPUT);
    attachInterrupt(PIN_PAPER, gpio_paper_isr, RISING);
}

int gpio_paper_check()
{
    return digitalRead(PIN_PAPER);
}

void gpio_init()
{
    gpio_led_init();
    gpio_paper_init();
    gpio_heater_init();
    gpio_button_init();
}