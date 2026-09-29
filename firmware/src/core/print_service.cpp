#include "print_service.h"

#include "em_config.h"
#include "em_device.h"
#include "hal/em_indicator.h"
#include "hal/em_printer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace {
constexpr uint32_t MANUAL_FEED_SESSION = 0xFFFFFFFFu;
constexpr TickType_t FEED_TICKS = pdMS_TO_TICKS(2) > 0 ? pdMS_TO_TICKS(2) : 1;

typedef enum {
    PRINT_COMMAND_BEGIN = 0,
    PRINT_COMMAND_LINE,
    PRINT_COMMAND_END,
    PRINT_COMMAND_CANCEL,
    PRINT_COMMAND_FEED_START,
    PRINT_COMMAND_FEED_STOP,
} print_command_type_e;

typedef struct {
    print_command_type_e type;
    uint32_t session;
    uint8_t density;
    printer_error_e error;
    uint8_t line[TPH_DI_LEN];
} print_command_t;

QueueHandle_t command_queue = nullptr;
SemaphoreHandle_t session_mutex = nullptr;
uint32_t active_session = 0;

TickType_t timeout_ticks(uint32_t timeout_ms)
{
    return timeout_ms == UINT32_MAX
               ? portMAX_DELAY
               : pdMS_TO_TICKS(timeout_ms);
}

bool reserve_session(uint32_t session)
{
    if (session == 0)
        return false;

    if (xSemaphoreTake(session_mutex, portMAX_DELAY) != pdTRUE)
        return false;

    bool reserved = false;
    if (active_session == 0)
    {
        active_session = session;
        reserved = true;
    }

    xSemaphoreGive(session_mutex);
    return reserved;
}

bool session_is_active(uint32_t session)
{
    if (xSemaphoreTake(session_mutex, portMAX_DELAY) != pdTRUE)
        return false;

    const bool active = active_session == session;
    xSemaphoreGive(session_mutex);
    return active;
}

uint32_t current_session()
{
    if (xSemaphoreTake(session_mutex, portMAX_DELAY) != pdTRUE)
        return 0;

    const uint32_t session = active_session;
    xSemaphoreGive(session_mutex);
    return session;
}

void release_session(uint32_t session)
{
    if (xSemaphoreTake(session_mutex, portMAX_DELAY) != pdTRUE)
        return;

    if (active_session == session)
        active_session = 0;

    xSemaphoreGive(session_mutex);
}

bool send_command(const print_command_t &command, uint32_t timeout_ms, bool front = false)
{
    if (command_queue == nullptr)
        return false;

    const BaseType_t result =
        front
            ? xQueueSendToFront(command_queue, &command, timeout_ticks(timeout_ms))
            : xQueueSend(command_queue, &command, timeout_ticks(timeout_ms));

    return result == pdTRUE;
}

void set_idle(printer_error_e error = PRINTER_ERROR_NONE)
{
    set_device_printer_status(PRINTER_STATUS_IDLE);
    set_device_printer_error(error);
}

void warn()
{
    indicator_beep(INDICATOR_WARN);
    indicator_led(INDICATOR_WARN);
}

void fail_job(uint32_t session, printer_error_e error)
{
    Serial.printf("Printer job failed: session=%lu error=%d\n",
                  static_cast<unsigned long>(session), error);
    printer_abort();
    xQueueReset(command_queue);

    set_device_printer_status(PRINTER_STATUS_ERROR);
    set_device_printer_error(error);
    release_session(session);

    warn();
}

void printer_task(void *)
{
    uint32_t line_number = 0;

    for (;;)
    {
        print_command_t command;
        const device_state_t state = get_device_state();
        const TickType_t wait =
            state.printer_state == PRINTER_STATUS_PRINTING
                ? pdMS_TO_TICKS(250)
                : state.printer_state == PRINTER_STATUS_FEEDING
                      ? FEED_TICKS
                      : portMAX_DELAY;

        if (xQueueReceive(command_queue, &command, wait) != pdTRUE)
        {
            if (state.printer_state == PRINTER_STATUS_PRINTING ||
                state.printer_state == PRINTER_STATUS_FEEDING)
            {
                const printer_error_e error =
                    state.printer_state == PRINTER_STATUS_PRINTING
                        ? printer_poll()
                        : printer_feed_step();
                if (error != PRINTER_ERROR_NONE)
                {
                    const uint32_t session = current_session();
                    if (session != 0)
                        fail_job(session, error);
                }
            }
            continue;
        }

        switch (command.type)
        {
        case PRINT_COMMAND_BEGIN:
        {
            if (!session_is_active(command.session))
                break;

            set_device_printer_error(PRINTER_ERROR_NONE);
            printer_set_density(command.density);

            const printer_error_e error = printer_begin();
            if (error != PRINTER_ERROR_NONE)
            {
                fail_job(command.session, error);
                break;
            }

            line_number = 0;
            set_device_printer_status(PRINTER_STATUS_PRINTING);
            indicator_beep(INDICATOR_PRINT_START);
            indicator_led(INDICATOR_PRINT_START);
            break;
        }

        case PRINT_COMMAND_LINE:
        {
            if (!session_is_active(command.session))
                break;

            const device_state_t state = get_device_state();
            if (state.printer_state != PRINTER_STATUS_PRINTING)
                break;

            const printer_error_e error =
                printer_write_line(command.line, line_number++);
            if (error != PRINTER_ERROR_NONE)
                fail_job(command.session, error);
            break;
        }

        case PRINT_COMMAND_END:
        {
            if (!session_is_active(command.session))
                break;

            const device_state_t state = get_device_state();
            if (state.printer_state == PRINTER_STATUS_PRINTING)
            {
                const printer_error_e error = printer_end(PRINT_TRAILING_FEED_STEPS);
                if (error != PRINTER_ERROR_NONE)
                {
                    fail_job(command.session, error);
                    break;
                }
            }
            else
                printer_abort();

            Serial.printf("Printer job completed: session=%lu lines=%lu\n",
                          static_cast<unsigned long>(command.session),
                          static_cast<unsigned long>(line_number));
            set_idle();
            release_session(command.session);
            break;
        }

        case PRINT_COMMAND_CANCEL:
            if (!session_is_active(command.session))
                break;

            printer_abort();
            xQueueReset(command_queue);

            if (command.error == PRINTER_ERROR_CANCELLED)
            {
                set_idle(PRINTER_ERROR_CANCELLED);
            }
            else
            {
                set_device_printer_status(PRINTER_STATUS_ERROR);
                set_device_printer_error(command.error);
            }

            release_session(command.session);
            break;

        case PRINT_COMMAND_FEED_START:
        {
            if (!session_is_active(MANUAL_FEED_SESSION))
                break;

            const printer_error_e error = printer_feed_start();
            if (error != PRINTER_ERROR_NONE)
            {
                set_device_printer_status(PRINTER_STATUS_ERROR);
                set_device_printer_error(error);
                release_session(MANUAL_FEED_SESSION);
                warn();
                break;
            }

            set_device_printer_error(PRINTER_ERROR_NONE);
            set_device_printer_status(PRINTER_STATUS_FEEDING);
            break;
        }

        case PRINT_COMMAND_FEED_STOP:
            if (!session_is_active(MANUAL_FEED_SESSION))
                break;

            printer_feed_stop();
            set_idle();
            release_session(MANUAL_FEED_SESSION);
            break;
        }
    }
}
} // namespace

bool print_service_init()
{
    command_queue = xQueueCreate(PRINT_QUEUE_DEPTH, sizeof(print_command_t));
    session_mutex = xSemaphoreCreateMutex();

    if (command_queue == nullptr || session_mutex == nullptr)
        return false;

    return xTaskCreate(
               printer_task,
               "Printer",
               4096,
               nullptr,
               2,
               nullptr) == pdPASS;
}

bool print_service_begin(uint32_t session, uint8_t density, uint32_t timeout_ms)
{
    if (!reserve_session(session))
        return false;

    print_command_t command = {};
    command.type = PRINT_COMMAND_BEGIN;
    command.session = session;
    command.density = density;

    if (send_command(command, timeout_ms))
        return true;

    release_session(session);
    return false;
}

bool print_service_line(uint32_t session, const uint8_t *line, uint32_t timeout_ms)
{
    if (line == nullptr || !session_is_active(session))
        return false;

    print_command_t command = {};
    command.type = PRINT_COMMAND_LINE;
    command.session = session;
    memcpy(command.line, line, TPH_DI_LEN);

    return send_command(command, timeout_ms);
}

bool print_service_end(uint32_t session, uint32_t timeout_ms)
{
    if (!session_is_active(session))
        return false;

    print_command_t command = {};
    command.type = PRINT_COMMAND_END;
    command.session = session;

    return send_command(command, timeout_ms);
}

bool print_service_cancel(uint32_t session, uint32_t timeout_ms)
{
    return print_service_abort(
        session,
        PRINTER_ERROR_CANCELLED,
        timeout_ms);
}

bool print_service_abort(
    uint32_t session,
    printer_error_e error,
    uint32_t timeout_ms)
{
    if (!session_is_active(session))
        return false;

    print_command_t command = {};
    command.type = PRINT_COMMAND_CANCEL;
    command.session = session;
    command.error = error;

    return send_command(command, timeout_ms, true);
}

bool print_service_feed_start(uint32_t timeout_ms)
{
    if (!reserve_session(MANUAL_FEED_SESSION))
        return false;

    print_command_t command = {};
    command.type = PRINT_COMMAND_FEED_START;
    command.session = MANUAL_FEED_SESSION;

    if (send_command(command, timeout_ms))
        return true;

    release_session(MANUAL_FEED_SESSION);
    return false;
}

bool print_service_feed_stop(uint32_t timeout_ms)
{
    if (!session_is_active(MANUAL_FEED_SESSION))
        return false;

    print_command_t command = {};
    command.type = PRINT_COMMAND_FEED_STOP;
    command.session = MANUAL_FEED_SESSION;

    return send_command(command, timeout_ms, true);
}
