#include "tcp_transport.h"

#include "em_config.h"

#if WIFI_ENABLED

#include "core/print_service.h"
#include "network/wifi_manager.h"
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
} tcp_context_t;

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
    tcp_context_t *context =
        static_cast<tcp_context_t *>(context_ptr);

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

void tcp_task(void *)
{
    server.begin();
    Serial.printf("Print TCP server listening on %d\n", WIFI_PRINT_PORT);

    for (;;)
    {
        WiFiClient client = server.available();
        if (!client)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (wifi_manager_setup_active())
        {
            client.stop();
            continue;
        }

        client.setNoDelay(true);

        tcp_context_t context = {
            next_session(),
            &client,
            false,
        };

        print_protocol_parser_t parser;
        print_protocol_init(&parser, handle_command, &context);

        Serial.printf("TCP client connected, session=%u\n", context.session);

        bool accepted = true;
        uint8_t buffer[128];
        uint32_t last_activity = millis();

        while (client.connected() &&
               accepted &&
               !wifi_manager_setup_active())
        {
            const int available = client.available();
            if (available <= 0)
            {
                if (static_cast<uint32_t>(millis() - last_activity) >=
                    WIFI_CLIENT_IDLE_TIMEOUT_MS)
                {
                    Serial.println("TCP client idle timeout");
                    break;
                }

                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }

            const size_t wanted =
                available < static_cast<int>(sizeof(buffer))
                    ? static_cast<size_t>(available)
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

        Serial.printf("TCP client disconnected, session=%u\n", context.session);
    }
}
} // namespace

void init_tcp_transport()
{
    xTaskCreate(
        tcp_task,
        "TCPPrint",
        6144,
        nullptr,
        1,
        nullptr);
}

#else

void init_tcp_transport()
{
}

#endif
