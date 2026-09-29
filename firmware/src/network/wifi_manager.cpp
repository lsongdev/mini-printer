#include "wifi_manager.h"

#include "em_config.h"

#if WIFI_ENABLED

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

namespace {
DNSServer dns_server;
Preferences preferences;
char ap_ssid[33] = {};
char hostname[33] = {};
bool initialized = false;

void make_names()
{
    const uint16_t suffix =
        static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFu);

    snprintf(
        ap_ssid,
        sizeof(ap_ssid),
        "%s-%04X",
        WIFI_AP_SSID,
        suffix);

    snprintf(
        hostname,
        sizeof(hostname),
        "mini-printer-%04x",
        suffix);
}

void connect_saved_network()
{
    if (!preferences.begin(WIFI_PREFS_NAMESPACE, true))
        return;

    const String ssid = preferences.getString("ssid");
    const String password = preferences.getString("password");
    preferences.end();

    if (ssid.length() == 0)
        return;

    Serial.printf("Connecting to saved Wi-Fi: %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
}

void dns_task(void *)
{
    for (;;)
    {
        dns_server.processNextRequest();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool save_credentials(const char *ssid, const char *password)
{
    if (!preferences.begin(WIFI_PREFS_NAMESPACE, false))
        return false;

    const char *saved_password = password == nullptr ? "" : password;
    const size_t ssid_written = preferences.putString("ssid", ssid);
    const size_t password_written =
        preferences.putString("password", saved_password);

    preferences.end();
    return ssid_written == strlen(ssid) &&
           password_written == strlen(saved_password);
}

void restore_saved_network()
{
    if (!preferences.begin(WIFI_PREFS_NAMESPACE, true))
        return;

    const String ssid = preferences.getString("ssid");
    const String password = preferences.getString("password");
    preferences.end();

    if (ssid.length() != 0)
        WiFi.begin(ssid.c_str(), password.c_str());
}
} // namespace

bool wifi_manager_init()
{
    if (initialized)
        return true;

    make_names();

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(hostname);

    // Preferences is the source of truth. Clear the Arduino Wi-Fi driver's
    // remembered STA credentials so a failed setup attempt cannot survive a
    // reboot independently of our validated credentials.
    WiFi.disconnect(false, true);

    bool ap_started = false;
    if (strlen(WIFI_AP_PASSWORD) >= 8)
        ap_started = WiFi.softAP(ap_ssid, WIFI_AP_PASSWORD);
    else
        ap_started = WiFi.softAP(ap_ssid);

    if (!ap_started)
    {
        Serial.println("Failed to start Wi-Fi AP");
        return false;
    }

    dns_server.start(53, "*", WiFi.softAPIP());

    if (MDNS.begin(hostname))
    {
        MDNS.addService("http", "tcp", WIFI_HTTP_PORT);
        MDNS.addService("miniprinter", "tcp", WIFI_PRINT_PORT);
    }
    else
    {
        Serial.println("Failed to start mDNS");
    }

    Serial.printf(
        "Wi-Fi AP ready: %s, IP=%s, host=%s.local\n",
        ap_ssid,
        WiFi.softAPIP().toString().c_str(),
        hostname);

    connect_saved_network();

    if (xTaskCreate(
            dns_task,
            "CaptiveDNS",
            3072,
            nullptr,
            1,
            nullptr) != pdPASS)
    {
        Serial.println("Failed to start captive DNS task");
        return false;
    }

    initialized = true;
    return true;
}

bool wifi_manager_connect(
    const char *ssid,
    const char *password,
    uint32_t timeout_ms)
{
    if (!initialized || ssid == nullptr || ssid[0] == '\0')
        return false;

    WiFi.begin(ssid, password == nullptr ? "" : password);

    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (static_cast<uint32_t>(millis() - started) >= timeout_ms)
        {
            WiFi.disconnect(false, true);
            restore_saved_network();
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!save_credentials(ssid, password))
        Serial.println("Wi-Fi connected, but credentials could not be saved");

    Serial.printf(
        "Wi-Fi STA connected: %s, IP=%s\n",
        WiFi.SSID().c_str(),
        WiFi.localIP().toString().c_str());

    return true;
}

void wifi_manager_forget()
{
    if (preferences.begin(WIFI_PREFS_NAMESPACE, false))
    {
        preferences.clear();
        preferences.end();
    }

    WiFi.disconnect(false, true);
}

wifi_status_t wifi_manager_status()
{
    wifi_status_t status = {};
    status.connected = WiFi.status() == WL_CONNECTED;

    if (!status.connected)
        return status;

    const String ssid = WiFi.SSID();
    strlcpy(status.ssid, ssid.c_str(), sizeof(status.ssid));
    status.ip = WiFi.localIP();
    status.rssi = WiFi.RSSI();
    return status;
}

const char *wifi_manager_ap_ssid()
{
    return ap_ssid;
}

const char *wifi_manager_hostname()
{
    return hostname;
}

#else

bool wifi_manager_init()
{
    return false;
}

bool wifi_manager_connect(const char *, const char *, uint32_t)
{
    return false;
}

void wifi_manager_forget()
{
}

wifi_status_t wifi_manager_status()
{
    return {};
}

const char *wifi_manager_ap_ssid()
{
    return "";
}

const char *wifi_manager_hostname()
{
    return "";
}

#endif
