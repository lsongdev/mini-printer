#include <Arduino.h>
#include <SPI.h>

// 打印头数据引脚
#define PIN_SCK 15
#define PIN_SDA 13
#define PIN_LATCH 12
#define LATCH_TIME_US 1
static const int SPI_CLK = 1000000; // 1 MHz

SPIClass hspi = SPIClass(HSPI);
SPISettings settings = SPISettings(SPI_CLK, SPI_MSBFIRST, SPI_MODE0);

void spi_init()
{
  // 初始化SPI总线
  hspi.begin(PIN_SCK, -1, PIN_SDA, -1);
  hspi.setFrequency(SPI_CLK);

  // 初始化打印头锁存引脚
  pinMode(PIN_LATCH, OUTPUT);
  digitalWrite(PIN_LATCH, HIGH);
}

void spi_send(void *data, int size)
{
  hspi.beginTransaction(settings);
  hspi.transfer(data, size);
  hspi.endTransaction();

  digitalWrite(PIN_LATCH, LOW);
  delayMicroseconds(LATCH_TIME_US);
  digitalWrite(PIN_LATCH, HIGH);
}