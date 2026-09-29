#include "wifi_transport.h"

#include "em_config.h"

#if WIFI_PRINT_ENABLED

#include "core/print_service.h"
#include "protocol/print_protocol.h"
#include "transport_common.h"

#include <WiFi.h>

namespace {
WiFiServer server(WIFI_PRINT_PORT);
uint32_t session_counter = 0;

typedef struct {
    uint32_t session;
    WiFiClient *client;
    bool job_open;
} wifi_context_t;

uint32_t next_session()
{
    ++session_counter;

    uint32_t value = 0x80000000u | session_counter;
    if (value == 0xFFFFFFFFu)
    {
        ++session_counter;
        value = 0x80000000u | session_counter;
    }

    return value;
}

bool send_status(WiFiClient &client)
{
    uint8_t frame[PRINT_PROTOCOL_HEADER_SIZE + 5];
    const size_t length = transport_encode_status(frame, sizeof(frame));

    return length > 0 && client.write(frame, length) == length;
}

bool handle_command(
    const print_protocol_command_t *command,
    void *context_ptr)
{
    wifi_context_t *context =
        static_cast<wifi_context_t *>(context_ptr);

    if (command->type == PRINT_PROTOCOL_STATUS_REQUEST)
        return send_status(*context->client);

    const bool accepted = transport_submit_print_command(
        command,
        context->session,
        5000);

    if (!accepted)
        return false;

    if (command->type == PRINT_PROTOCOL_BEGIN)
        context->job_open = true;
    else if (command->type == PRINT_PROTOCOL_END ||
             command->type == PRINT_PROTOCOL_CANCEL)
        context->job_open = false;

    return true;
}

void wifi_task(void *)
{
    server.begin();
    Serial.printf("Wi-Fi print TCP server listening on %d\n", WIFI_PRINT_PORT);

    for (;;)
    {
        WiFiClient client = server.available();
        if (!client)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        client.setNoDelay(true);

        wifi_context_t context = {
            next_session(),
            &client,
            false,
        };

        print_protocol_parser_t parser;
        print_protocol_init(&parser, handle_command, &context);

        Serial.printf("Wi-Fi client connected, session=%u\n", context.session);

        bool accepted = true;
        uint8_t buffer[128];
        uint32_t last_activity = millis();

        while (client.connected() && accepted)
        {
            const int available = client.available();
            if (available <= 0)
            {
                if (millis() - last_activity >= WIFI_CLIENT_IDLE_TIMEOUT_MS)
                {
                    Serial.println("Wi-Fi client idle timeout");
                    break;
                }

                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            const size_t wanted =
                available < static_cast<int>(sizeof(buffer))
                    ? available
                    : sizeof(buffer);

            const int received = client.read(buffer, wanted);
            if (received <= 0)
                continue;

            last_activity = millis();

            accepted = print_protocol_feed(
                &parser,
                buffer,
                static_cast<size_t>(received));
        }

        if (context.job_open)
        {
            const print_protocol_error_e error = print_protocol_error(&parser);
            if (!accepted && error == PRINT_PROTOCOL_ERROR_INVALID_FRAME)
                print_service_abort(context.session, PRINTER_ERROR_PROTOCOL, 1000);
            else
                print_service_cancel(context.session, 1000);
        }

        client.stop();

        Serial.printf("Wi-Fi client disconnected, session=%u\n", context.session);
    }
}
} // namespace

void init_wifi_transport()
{
    char ssid[33];
    const uint16_t suffix =
        static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFu);

    snprintf(
        ssid,
        sizeof(ssid),
        "%s-%04X",
        WIFI_AP_SSID,
        suffix);

    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    bool started = false;
    if (strlen(WIFI_AP_PASSWORD) >= 8)
        started = WiFi.softAP(ssid, WIFI_AP_PASSWORD);
    else
        started = WiFi.softAP(ssid);

    if (!started)
    {
        Serial.println("Failed to start Wi-Fi AP");
        return;
    }

    Serial.printf(
        "Wi-Fi AP ready: %s, IP=%s\n",
        ssid,
        WiFi.softAPIP().toString().c_str());

    xTaskCreate(
        wifi_task,
        "WiFiPrint",
        6144,
        nullptr,
        1,
        nullptr);
}

#else

void init_wifi_transport()
{
}

#endif
