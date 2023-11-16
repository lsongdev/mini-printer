#include <Arduino.h>

#include "drivers/printer.h"

uint8_t myData[MAX_NLINES][NBYTES_PER_LINE];

void setup()
{
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");

  printer_init();

  for (int line = 0; line < 10; ++line)
  {
    for (int i = 0; i < NBYTES_PER_LINE; ++i)
    {
      myData[line][i] = 0xff;
    }
  }
  printer_send(myData, 10);
}

void loop()
{
}