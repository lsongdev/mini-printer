#include "em_config.h"

#if BLE_ENABLED

#include "ble_transport.h"

#include "core/print_service.h"
#include "em_config.h"
#include "protocol/print_protocol.h"
#include "transport_common.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <atomic>
#include <freertos/semphr.h>

namespace {
BLECharacteristic *characteristic = nullptr;
std::atomic<bool> connected{false};
uint32_t session_counter = 0;
uint32_t session = 0;
bool job_open = false;

SemaphoreHandle_t characteristic_mutex = nullptr;
print_protocol_parser_t parser;

constexpr size_t BLE_MAX_CHUNK = 512;

bool set_status_value(bool notify)
{
    if (characteristic == nullptr || characteristic_mutex == nullptr)
        return false;

    uint8_t frame[PRINT_PROTOCOL_HEADER_SIZE + 5];
    const size_t length = transport_encode_status(frame, sizeof(frame));
    if (length == 0)
        return false;

    if (xSemaphoreTake(characteristic_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    characteristic->setValue(frame, length);

    if (notify && connected.load())
        characteristic->notify();

    xSemaphoreGive(characteristic_mutex);
    return true;
}

bool handle_command(
    const print_protocol_command_t *command,
    void *)
{
    if (command->type == PRINT_PROTOCOL_STATUS_REQUEST)
        return set_status_value(true);

    const bool accepted =
        transport_submit_print_command(command, session, 1000);

    if (!accepted)
        return false;

    if (command->type == PRINT_PROTOCOL_BEGIN)
        job_open = true;
    else if (command->type == PRINT_PROTOCOL_END ||
             command->type == PRINT_PROTOCOL_CANCEL)
        job_open = false;

    return true;
}

class ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *) override
    {
        connected.store(true);

        ++session_counter;
        if (session_counter == 0)
            ++session_counter;
        session = session_counter;
        job_open = false;

        print_protocol_init(&parser, handle_command, nullptr);
        set_status_value(false);

        Serial.printf("BLE connected, session=%u\n", session);
    }

    void onDisconnect(BLEServer *server) override
    {
        const uint32_t old_session = session;

        connected.store(false);
        session = 0;
        print_protocol_reset(&parser);

        if (old_session != 0 && job_open)
            print_service_cancel(old_session, 1000);

        job_open = false;

        server->startAdvertising();
        Serial.println("BLE disconnected");
    }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
    void onRead(BLECharacteristic *) override
    {
        set_status_value(false);
    }

    void onWrite(BLECharacteristic *, esp_ble_gatts_cb_param_t *event) override
    {
        // Status notifications replace the characteristic value. The write
        // event's payload stays valid for this synchronous callback.
        const size_t length = event->write.len;

        if (session == 0 || length == 0)
            return;

        if (length > BLE_MAX_CHUNK)
        {
            Serial.println("BLE write too large");
            if (job_open)
                print_service_abort(session, PRINTER_ERROR_PROTOCOL, 1000);
            job_open = false;
            print_protocol_reset(&parser);
            return;
        }

        if (!print_protocol_feed(&parser, event->write.value, length))
        {
            const print_protocol_error_e error = print_protocol_error(&parser);
            Serial.printf("BLE protocol rejected frame, error=%d\n", error);

            if (job_open)
            {
                if (error == PRINT_PROTOCOL_ERROR_INVALID_FRAME)
                    print_service_abort(session, PRINTER_ERROR_PROTOCOL, 1000);
                else
                    print_service_cancel(session, 1000);
            }

            job_open = false;
            print_protocol_reset(&parser);
        }
    }
};
} // namespace

void init_ble_transport()
{
    characteristic_mutex = xSemaphoreCreateMutex();
    if (characteristic_mutex == nullptr)
    {
        Serial.println("Failed to create BLE characteristic mutex");
        return;
    }

    BLEDevice::init(BLE_NAME);

    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(SERVICE_UUID);

    characteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE);

    characteristic->setCallbacks(new CharacteristicCallbacks());
    characteristic->addDescriptor(new BLE2902());

    set_status_value(false);

    service->start();
    BLEDevice::startAdvertising();

    Serial.println("BLE transport ready");
}

bool ble_transport_connected()
{
    return connected.load();
}

void ble_transport_report_status()
{
    if (connected.load())
        set_status_value(true);
}

#endif // BLE_ENABLED
