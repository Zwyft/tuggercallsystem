#pragma once
#include <cstdint>

class MockSPI {
public:
  void begin(int sclk, int miso, int mosi, int nss) {}
  void beginTransaction(void *) {}
  void endTransaction() {}
  void setFrequency(uint32_t freq) {}
  uint8_t transfer(uint8_t data) { return 0; }
};

extern MockSPI SPI;
