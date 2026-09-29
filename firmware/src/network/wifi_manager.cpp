#include "wifi_manager.h"

#include "em_config.h"

#if WIFI_ENABLED

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace {
Preferences preferences;
char ap_ssid[33] = {};
char hostname[33] = {};
char saved_ssid[33] = {};
bool initialized = false;

constexpr char CREDENTIALS_KEY[] = "network";
constexpr uint8_t CREDENTIALS_VERSION = 1;

struct saved_network_t {
    uint8_t version;
    char ssid[33];
    char password[65];
};

bool valid_network(const saved_network_t &network)
{
    return network.version == CREDENTIALS_VERSION &&
           network.ssid[0] != '\0' &&
           memchr(network.ssid, '\0', sizeof(network.ssid)) != nullptr &&
           memchr(network.password, '\0', sizeof(network.password)) != nullptr;
}

bool read_saved_network(saved_network_t *network)
{
    if (network == nullptr ||
        !preferences.begin(WIFI_PREFS_NAMESPACE, true))
        return false;

    *network = {};
    bool loaded = false;

    if (preferences.isKey(CREDENTIALS_KEY))
    {
        loaded = preferences.getBytesLength(CREDENTIALS_KEY) == sizeof(*network) &&
                 preferences.getBytes(CREDENTIALS_KEY, network, sizeof(*network)) == sizeof(*network) &&
                 valid_network(*network);
    }
    if (!loaded && preferences.isKey("ssid"))
    {
        // Read credentials saved by earlier firmware releases.
        const String ssid = preferences.getString("ssid");
        const String password = preferences.getString("password");
        if (ssid.length() > 0 && ssid.length() <= 32 && password.length() <= 64)
        {
            network->version = CREDENTIALS_VERSION;
            strlcpy(network->ssid, ssid.c_str(), sizeof(network->ssid));
            strlcpy(network->password, password.c_str(), sizeof(network->password));
            loaded = true;
        }
    }

    preferences.end();
    return loaded;
}

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
    saved_network_t network = {};
    if (!read_saved_network(&network))
        return;

    strlcpy(saved_ssid, network.ssid, sizeof(saved_ssid));
    Serial.printf("Connecting to saved Wi-Fi: %s\n", network.ssid);
    WiFi.begin(network.ssid, network.password);
}

void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("Wi-Fi STA ready: IP=%s, heap=%u, largest=%u\n",
                      WiFi.localIP().toString().c_str(),
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.printf("Wi-Fi STA disconnected: reason=%u, heap=%u\n",
                      info.wifi_sta_disconnected.reason, ESP.getFreeHeap());
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.println("Wi-Fi AP client connected");
        break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.println("Wi-Fi AP client received an IP address");
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("Wi-Fi AP client disconnected");
        break;
    default:
        break;
    }
}

bool save_credentials(const char *ssid, const char *password)
{
    saved_network_t network = {};
    network.version = CREDENTIALS_VERSION;
    if (strlcpy(network.ssid, ssid, sizeof(network.ssid)) >= sizeof(network.ssid) ||
        strlcpy(network.password, password == nullptr ? "" : password, sizeof(network.password)) >= sizeof(network.password))
        return false;

    if (!preferences.begin(WIFI_PREFS_NAMESPACE, false))
        return false;

    const bool saved = preferences.putBytes(CREDENTIALS_KEY, &network, sizeof(network)) == sizeof(network);
    preferences.end();
    if (saved)
        strlcpy(saved_ssid, network.ssid, sizeof(saved_ssid));
    return saved;
}

void restore_saved_network()
{
    saved_network_t network = {};
    if (read_saved_network(&network))
        WiFi.begin(network.ssid, network.password);
}
} // namespace

bool wifi_manager_init()
{
    if (initialized)
        return true;

    make_names();

    WiFi.onEvent(on_wifi_event);
    WiFi.setHostname(hostname);

    if (!WiFi.mode(WIFI_AP_STA))
    {
        Serial.println("Failed to initialize Wi-Fi AP+STA mode");
        return false;
    }

    // ESP32 Wi-Fi and Bluetooth share the 2.4 GHz radio. The coexistence
    // controller requires Wi-Fi modem sleep; WIFI_PS_NONE causes an abort
    // as soon as both stacks are enabled.
    // Arduino's WiFi.setSleep() returns false when the requested mode is
    // already selected, which is the default on ESP32. Check the driver
    // operation itself so an unchanged setting is not treated as a failure.
    const esp_err_t sleep_result = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (sleep_result != ESP_OK)
    {
        Serial.printf(
            "Failed to enable Wi-Fi modem sleep: %s\n",
            esp_err_to_name(sleep_result));
        return false;
    }

    WiFi.setAutoReconnect(true);

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

    initialized = true;
    return true;
}

wifi_connect_result_e wifi_manager_connect(
    const char *ssid,
    const char *password,
    uint32_t timeout_ms)
{
    if (!initialized || ssid == nullptr || ssid[0] == '\0')
        return wifi_connect_result_e::connection_failed;

    // Force a real transition even if STA is already connected; otherwise
    // WL_CONNECTED can briefly describe the previous network and make a new
    // credential set look successful.
    WiFi.disconnect(false, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    WiFi.begin(ssid, password == nullptr ? "" : password);

    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (static_cast<uint32_t>(millis() - started) >= timeout_ms)
        {
            WiFi.disconnect(false, true);
            restore_saved_network();
            return wifi_connect_result_e::connection_failed;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!save_credentials(ssid, password))
    {
        Serial.println("Wi-Fi connected, but credentials could not be saved");
        return wifi_connect_result_e::save_failed;
    }

    Serial.printf(
        "Wi-Fi STA connected: %s, IP=%s\n",
        WiFi.SSID().c_str(),
        WiFi.localIP().toString().c_str());

    return wifi_connect_result_e::connected_and_saved;
}

bool wifi_manager_forget()
{
    if (!preferences.begin(WIFI_PREFS_NAMESPACE, false))
        return false;

    const bool cleared = preferences.clear();
    preferences.end();
    if (!cleared)
        return false;

    saved_ssid[0] = '\0';
    WiFi.disconnect(false, true);
    return true;
}

wifi_status_t wifi_manager_status()
{
    wifi_status_t status = {};
    strlcpy(status.saved_ssid, saved_ssid, sizeof(status.saved_ssid));
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

wifi_connect_result_e wifi_manager_connect(const char *, const char *, uint32_t)
{
    return wifi_connect_result_e::connection_failed;
}

bool wifi_manager_forget()
{
    return false;
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
