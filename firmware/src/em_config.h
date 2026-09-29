#ifndef _EM_CONFIG_H_
#define _EM_CONFIG_H_

#include <Arduino.h>

#ifndef BLE_ENABLED
#define BLE_ENABLED 1
#endif

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_NAME "Mini-Printer"

// Wi-Fi runs in AP+STA mode. The AP is always available as a setup and
// recovery path while STA credentials are stored in NVS after a successful
// connection.
#define WIFI_ENABLED 1
#define WIFI_AP_SSID "Mini-Printer"
#define WIFI_AP_PASSWORD ""
#define WIFI_HTTP_PORT 80
#define WIFI_PRINT_PORT 9100
#define WIFI_CLIENT_IDLE_TIMEOUT_MS 30000
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_PREFS_NAMESPACE "wifi"

#define DEFAULT_PRINT_DENSITY 64
#define PRINT_QUEUE_DEPTH 16
#define PRINT_TRAILING_FEED_STEPS 230

// 按键引脚
// 34 35 36 39 仅可以作为输入 INPUT, 不支持 INPUT_PULLUP
#define PIN_KEY 5
#define PIN_PAPER 35
#define PIN_BATTERY_ADC 34
#define PIN_ADC_EN 4
#define BATTERY_ADC_BIT 12
#define PIN_TEMPERATRUE 36
#define PIN_BEEP 18
#define PIN_LED 18

#define PIN_MOTOR_AP 23
#define PIN_MOTOR_AM 22
#define PIN_MOTOR_BP 21
#define PIN_MOTOR_BM 19

#define PIN_LAT 12
#define PIN_SCK 15
#define PIN_SPI_MISO 16
#define PIN_SDA 13

// 原厂 V4
#define PIN_STB1 26
#define PIN_STB2 27
#define PIN_STB3 14
#define PIN_STB4 32
#define PIN_STB5 33
#define PIN_STB6 25

// 打印头电源升压控制引脚
#define PIN_VHEN 17

#define PRINT_TIME 2600
#define PRINT_END_TIME 200
#define PRINTER_BOOST_SETTLE_MS 50
#define MOTOR_WATI_TIME 4000
#define LAT_TIME 1

#define PRINTER_MAX_TEMPERATURE_C 65
#define PRINTER_TEMPERATURE_CHECK_LINES 16
#define PRINTER_STALL_TIMEOUT_MS 20000

#define us_delay(us) delayMicroseconds(us)

#endif
