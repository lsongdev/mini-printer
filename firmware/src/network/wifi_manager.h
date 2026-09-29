#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <Arduino.h>

typedef struct {
    bool connected;
    char ssid[33];
    IPAddress ip;
    int32_t rssi;
} wifi_status_t;

bool wifi_manager_init();
bool wifi_manager_connect(const char *ssid, const char *password, uint32_t timeout_ms);
void wifi_manager_forget();
wifi_status_t wifi_manager_status();

const char *wifi_manager_ap_ssid();
const char *wifi_manager_hostname();

#endif
