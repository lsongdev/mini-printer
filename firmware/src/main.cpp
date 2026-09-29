#include <Arduino.h>

#include "em_task.h"

void setup()
{
    init_task();
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
