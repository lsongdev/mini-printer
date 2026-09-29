#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <Arduino.h>

enum class wifi_state_e {
    connecting,
    online,
    setup,
};

typedef struct {
    wifi_state_e state;
    bool connected;
    char ssid[33];
    char saved_ssid[33];
    IPAddress ip;
    int32_t rssi;
} wifi_status_t;

enum class wifi_connect_result_e {
    connected_and_saved,
    connection_failed,
    save_failed,
};

bool wifi_manager_init();
void wifi_manager_process();

bool wifi_manager_start_setup();
void wifi_manager_schedule_setup_stop(uint32_t delay_ms);

wifi_connect_result_e wifi_manager_connect(
    const char *ssid,
    const char *password,
    uint32_t timeout_ms);

bool wifi_manager_forget();
wifi_status_t wifi_manager_status();
wifi_state_e wifi_manager_state();
const char *wifi_manager_state_name();
bool wifi_manager_setup_active();

const char *wifi_manager_ap_ssid();
const char *wifi_manager_hostname();

#endif
