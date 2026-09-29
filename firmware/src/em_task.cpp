#include "em_task.h"

#include "core/print_service.h"
#include "core/test_page.h"
#include "em_device.h"
#include "hal/em_adc.h"
#include "hal/em_button.h"
#include "hal/em_indicator.h"
#include "hal/em_paper.h"
#include "hal/em_printer.h"
#include "network/web_server.h"
#include "network/wifi_manager.h"
#include "transport/ble_transport.h"
#include "transport/tcp_transport.h"

#include <atomic>
#include <math.h>

Button_t Button;
static std::atomic<bool> test_page_task_running{false};

void read_paper()
{
    set_device_paper_status(paper_present() ? PAPER_STATUS_NORMAL : PAPER_STATUS_LACK);
}

void read_temperature()
{
    float celsius = 0.0f;
    if (!adc_read_temperature_c(&celsius))
    {
        set_device_temperature(255);
        return;
    }
    const int rounded = static_cast<int>(lroundf(celsius));
    set_device_temperature(static_cast<uint8_t>(rounded < 0 ? 0 : rounded > 254 ? 254 : rounded));
}

void read_all_sensors()
{
    int adc_mv = 0;
    if (adc_read_battery_mv(&adc_mv))
    {
        const int battery_mv = adc_mv * 2;
        const int percent = (battery_mv - 3300) * 100 / 900;
        set_device_battery(static_cast<uint8_t>(percent < 0 ? 0 : percent > 100 ? 100 : percent));
    }
    else
        set_device_battery(0);
    read_temperature();
    read_paper();
}

void test_page_task(void *)
{
    if (!print_test_page())
    {
        Serial.println("Test page could not be printed");
        indicator_beep(INDICATOR_WARN);
        indicator_led(INDICATOR_WARN);
    }

    test_page_task_running.store(false);
    vTaskDelete(nullptr);
}

void Btn_Double_CallBack(void *)
{
    if (test_page_task_running.exchange(true))
        return;

    if (xTaskCreate(test_page_task, "TestPage", 4096, nullptr, 1, nullptr) != pdPASS)
    {
        test_page_task_running.store(false);
        indicator_beep(INDICATOR_WARN);
        indicator_led(INDICATOR_WARN);
    }
}

void Btn_Long_CallBack(void *)
{
    if (!print_service_feed_start(100))
    {
        indicator_beep(INDICATOR_WARN);
        indicator_led(INDICATOR_WARN);
    }
}

void Btn_Long_Release_CallBack(void *)
{
    print_service_feed_stop(100);
}

void init_key()
{
    Button_Create(&Button, PIN_KEY, LOW);

    Button_Attach(&Button, BUTTON_DOUBLE, Btn_Double_CallBack);
    Button_Attach(&Button, BUTTON_LONG, Btn_Long_CallBack);
    Button_Attach(&Button, BUTTON_LONG_RELEASE, Btn_Long_Release_CallBack);
}

void task_report(void *)
{
    TickType_t last_sensor = xTaskGetTickCount();
    TickType_t last_status = 0;

    for (;;)
    {
        const TickType_t now = xTaskGetTickCount();

        if (now - last_sensor >= pdMS_TO_TICKS(10000))
        {
            const device_state_t state = get_device_state();

            // The printer task owns ADC temperature sampling while printing.
            if (state.printer_state != PRINTER_STATUS_PRINTING)
                read_all_sensors();
            else
                read_paper();

            last_sensor = now;
        }

        if (paper_take_change())
        {
            read_paper();
            const device_state_t state = get_device_state();

            if (state.paper_state == PAPER_STATUS_LACK &&
                state.printer_state == PRINTER_STATUS_FEEDING)
            {
                print_service_feed_stop(0);
            }

            ble_transport_report_status();
        }

        if (now - last_status >= pdMS_TO_TICKS(1000))
        {
            ble_transport_report_status();
            last_status = now;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void task_button(void *)
{
    for (;;)
    {
        Button_Process();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void init_task()
{
    Serial.begin(115200);
    Serial.println("mini-printer firmware");

    init_device_state();
    paper_init();
    indicator_init();
    adc_init();
    read_all_sensors();
    printer_init();

    if (!print_service_init())
    {
        Serial.println("failed to initialize print service");
        return;
    }

    init_key();
    init_ble_transport();

    if (wifi_manager_init())
    {
        init_tcp_transport();
        init_web_server();
    }

    xTaskCreate(
        task_report,
        "Report",
        4096,
        nullptr,
        1,
        nullptr);

    xTaskCreate(
        task_button,
        "Button",
        4096,
        nullptr,
        1,
        nullptr);
}
