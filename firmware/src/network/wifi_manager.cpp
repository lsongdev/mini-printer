#include "wifi_manager.h"

#include "em_config.h"

#if WIFI_ENABLED

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <esp_wifi.h>

namespace {
Preferences preferences;
DNSServer dns_server;

char ap_ssid[33] = {};
char hostname[33] = {};
char saved_ssid[33] = {};

std::atomic<wifi_state_e> current_state{wifi_state_e::connecting};
std::atomic<bool> setup_start_requested{false};
std::atomic<bool> setup_stop_pending{false};
std::atomic<uint32_t> setup_stop_at{0};
std::atomic<bool> startup_fallback_pending{false};
std::atomic<uint32_t> startup_fallback_at{0};

bool initialized = false;
bool mdns_started = false;
bool dns_started = false;

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

bool wait_for_connection(uint32_t timeout_ms)
{
    const uint32_t started = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        if (static_cast<uint32_t>(millis() - started) >= timeout_ms)
            return false;

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return true;
}

void start_mdns()
{
    if (mdns_started || WiFi.status() != WL_CONNECTED)
        return;

    if (!MDNS.begin(hostname))
    {
        Serial.println("Failed to start mDNS");
        return;
    }

    MDNS.addService("http", "tcp", WIFI_HTTP_PORT);
    MDNS.addService("miniprinter", "tcp", WIFI_PRINT_PORT);
    mdns_started = true;
}

bool begin_saved_network(const saved_network_t &network)
{
    strlcpy(saved_ssid, network.ssid, sizeof(saved_ssid));
    Serial.printf("Connecting to saved Wi-Fi: %s\n", network.ssid);
    WiFi.begin(network.ssid, network.password);
    return true;
}

void restore_saved_network()
{
    saved_network_t network = {};
    if (!read_saved_network(&network))
        return;

    begin_saved_network(network);
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

    const bool saved =
        preferences.putBytes(CREDENTIALS_KEY, &network, sizeof(network)) == sizeof(network);

    preferences.end();

    if (saved)
        strlcpy(saved_ssid, network.ssid, sizeof(saved_ssid));

    return saved;
}

void stop_setup_now()
{
    if (current_state.load() != wifi_state_e::setup)
        return;

    setup_stop_pending.store(false);

    if (dns_started)
    {
        dns_server.stop();
        dns_started = false;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    if (WiFi.status() == WL_CONNECTED)
    {
        current_state.store(wifi_state_e::online);
        start_mdns();
        Serial.printf("Wi-Fi setup stopped; STA online at %s\n",
                      WiFi.localIP().toString().c_str());
        return;
    }

    current_state.store(wifi_state_e::connecting);
    restore_saved_network();
    Serial.println("Wi-Fi setup stopped; retrying saved network");
}

bool start_setup_now()
{
    startup_fallback_pending.store(false);

    if (current_state.load() == wifi_state_e::setup)
        return true;

    if (!WiFi.mode(WIFI_AP_STA))
    {
        Serial.println("Failed to enter Wi-Fi AP+STA setup mode");
        return false;
    }

    bool ap_started = false;

    if (strlen(WIFI_AP_PASSWORD) >= 8)
        ap_started = WiFi.softAP(ap_ssid, WIFI_AP_PASSWORD);
    else
        ap_started = WiFi.softAP(ap_ssid);

    if (!ap_started)
    {
        Serial.println("Failed to start Wi-Fi setup AP");
        return false;
    }

    dns_server.setErrorReplyCode(DNSReplyCode::NoError);
    dns_started = dns_server.start(53, "*", WiFi.softAPIP());

    if (!dns_started)
        Serial.println("Failed to start captive DNS");

    current_state.store(wifi_state_e::setup);

    /*
     * If credentials already exist, setup mode is a temporary recovery window.
     * A first-boot device with no credentials stays in setup until configured.
     */
    if (saved_ssid[0] != '\0')
    {
        setup_stop_at.store(millis() + WIFI_SETUP_TIMEOUT_MS);
        setup_stop_pending.store(true);
    }
    else
    {
        setup_stop_pending.store(false);
    }

    Serial.printf(
        "Wi-Fi setup ready: %s, IP=%s\n",
        ap_ssid,
        WiFi.softAPIP().toString().c_str());

    return true;
}

void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        if (current_state.load() != wifi_state_e::setup)
            current_state.store(wifi_state_e::online);

        Serial.printf(
            "Wi-Fi STA ready: IP=%s, heap=%u, largest=%u\n",
            WiFi.localIP().toString().c_str(),
            ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (current_state.load() == wifi_state_e::online)
            current_state.store(wifi_state_e::connecting);

        Serial.printf(
            "Wi-Fi STA disconnected: reason=%u, heap=%u\n",
            info.wifi_sta_disconnected.reason,
            ESP.getFreeHeap());
        break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        Serial.println("Wi-Fi setup client connected");
        break;

    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
        Serial.println("Wi-Fi setup client received an IP address");
        break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        Serial.println("Wi-Fi setup client disconnected");
        break;

    default:
        break;
    }
}
} // namespace

bool wifi_manager_init()
{
    if (initialized)
        return true;

    make_names();

    WiFi.onEvent(on_wifi_event);
    WiFi.setHostname(hostname);

    if (!WiFi.mode(WIFI_STA))
    {
        Serial.println("Failed to initialize Wi-Fi STA mode");
        return false;
    }

    /*
     * ESP32 Wi-Fi and Bluetooth share the 2.4 GHz radio. BLE coexistence
     * requires Wi-Fi modem sleep; WIFI_PS_NONE aborts when both are enabled.
     */
    const esp_err_t sleep_result = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (sleep_result != ESP_OK)
    {
        Serial.printf(
            "Failed to enable Wi-Fi modem sleep: %s\n",
            esp_err_to_name(sleep_result));
        return false;
    }

    WiFi.setAutoReconnect(true);

    /*
     * Preferences is the source of truth. Do not allow Arduino's Wi-Fi driver
     * to reconnect using credentials outside our validated record.
     */
    WiFi.disconnect(false, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    initialized = true;

    saved_network_t network = {};
    if (!read_saved_network(&network))
        return start_setup_now();

    begin_saved_network(network);
    current_state.store(wifi_state_e::connecting);
    startup_fallback_at.store(millis() + WIFI_STARTUP_CONNECT_TIMEOUT_MS);
    startup_fallback_pending.store(true);

    Serial.printf(
        "Wi-Fi STA connecting; setup fallback in %u ms\n",
        static_cast<unsigned>(WIFI_STARTUP_CONNECT_TIMEOUT_MS));

    return true;
}

void wifi_manager_process()
{
    if (!initialized)
        return;

    if (setup_start_requested.exchange(false))
        start_setup_now();

    const wifi_state_e state = current_state.load();

    if (state == wifi_state_e::online)
    {
        startup_fallback_pending.store(false);
        start_mdns();
        return;
    }

    if (state == wifi_state_e::connecting)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            current_state.store(wifi_state_e::online);
            startup_fallback_pending.store(false);
            start_mdns();
            return;
        }

        if (startup_fallback_pending.load())
        {
            const uint32_t deadline = startup_fallback_at.load();

            if (static_cast<int32_t>(millis() - deadline) >= 0)
            {
                startup_fallback_pending.store(false);
                Serial.println("Saved Wi-Fi unavailable; entering setup mode");
                WiFi.disconnect(false, true);
                start_setup_now();
            }
        }

        return;
    }

    if (state != wifi_state_e::setup)
        return;

    if (dns_started)
        dns_server.processNextRequest();

    if (!setup_stop_pending.load())
        return;

    const uint32_t deadline = setup_stop_at.load();
    if (static_cast<int32_t>(millis() - deadline) >= 0)
        stop_setup_now();
}

bool wifi_manager_start_setup()
{
    if (!initialized)
        return false;

    if (current_state.load() == wifi_state_e::setup)
        return true;

    setup_start_requested.store(true);
    return true;
}

void wifi_manager_schedule_setup_stop(uint32_t delay_ms)
{
    if (current_state.load() != wifi_state_e::setup)
        return;

    setup_stop_at.store(millis() + delay_ms);
    setup_stop_pending.store(true);
}

wifi_connect_result_e wifi_manager_connect(
    const char *ssid,
    const char *password,
    uint32_t timeout_ms)
{
    if (!initialized ||
        current_state.load() != wifi_state_e::setup ||
        ssid == nullptr ||
        ssid[0] == '\0')
    {
        return wifi_connect_result_e::connection_failed;
    }

    saved_network_t previous = {};
    const bool had_previous = read_saved_network(&previous);

    WiFi.disconnect(false, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    WiFi.begin(ssid, password == nullptr ? "" : password);

    if (!wait_for_connection(timeout_ms))
    {
        WiFi.disconnect(false, true);

        if (had_previous)
            begin_saved_network(previous);

        return wifi_connect_result_e::connection_failed;
    }

    if (!save_credentials(ssid, password))
    {
        Serial.println("Wi-Fi connected, but credentials could not be saved");

        WiFi.disconnect(false, true);
        if (had_previous)
            begin_saved_network(previous);

        return wifi_connect_result_e::save_failed;
    }

    start_mdns();

    Serial.printf(
        "Wi-Fi STA connected during setup: %s, IP=%s\n",
        WiFi.SSID().c_str(),
        WiFi.localIP().toString().c_str());

    return wifi_connect_result_e::connected_and_saved;
}

bool wifi_manager_forget()
{
    if (!initialized || current_state.load() != wifi_state_e::setup)
        return false;

    if (!preferences.begin(WIFI_PREFS_NAMESPACE, false))
        return false;

    const bool cleared = preferences.clear();
    preferences.end();

    if (!cleared)
        return false;

    saved_ssid[0] = '\0';
    setup_stop_pending.store(false);
    WiFi.disconnect(false, true);
    return true;
}

wifi_status_t wifi_manager_status()
{
    wifi_status_t status = {};
    status.state = current_state.load();
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

wifi_state_e wifi_manager_state()
{
    return current_state.load();
}

const char *wifi_manager_state_name()
{
    switch (current_state.load())
    {
    case wifi_state_e::connecting:
        return "connecting";
    case wifi_state_e::online:
        return "online";
    case wifi_state_e::setup:
        return "setup";
    default:
        return "unknown";
    }
}

bool wifi_manager_setup_active()
{
    return current_state.load() == wifi_state_e::setup;
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

void wifi_manager_process()
{
}

bool wifi_manager_start_setup()
{
    return false;
}

void wifi_manager_schedule_setup_stop(uint32_t)
{
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

wifi_state_e wifi_manager_state()
{
    return wifi_state_e::connecting;
}

const char *wifi_manager_state_name()
{
    return "disabled";
}

bool wifi_manager_setup_active()
{
    return false;
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
