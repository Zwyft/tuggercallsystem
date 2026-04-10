#pragma once
#include "Arduino.h"
#include "SPI.h"
#include <queue>
#include <string>

// Constants
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_SX126X_IRQ_RX_DONE 0x02 // Val doesn't matter much
#define RADIOLIB_SX126X_IRQ_ALL 0xFF

class Module {
public:
  Module() {}
  Module(int cs, int dio1, int rst, int busy) {}
  Module(int, int, int, int, MockSPI &) {}
};

class SX1262 {
public:
  SX1262(Module *mod) {
    delete mod;
  } // Take ownership? No, usually passed by value or ref in Arduino... wait,
    // code says `new Module(...)`.
  // Code: `SX1262 radio = new Module(...)` -> This is weird in C++.
  // Code line: `SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST,
  // RADIO_BUSY, SPI);` This looks like `SX1262` constructor takes `Module*`.

  int begin(float freq, float bw, int sf, int cr, int sync, int pwr, int pre) {
    std::cout << "[Radio] Begin Freq:" << freq << " BW:" << bw << std::endl;
    return RADIOLIB_ERR_NONE;
  }

  void setDio2AsRfSwitch(bool) {}
  void setTCXO(float) {}
  void setDio1Action(void (*func)(void)) { callback = func; }

  void startReceive() {
    std::cout << "[Radio] Listening..." << std::endl;
    // In simulation, if we have a pending specific packet, trigger callback?
    if (!rxQueue.empty()) {
      // Simulate async delay or immediate trigger?
      // For simple tests, we can trigger manually in main loop or here
      // Triggering immediate might break 'loop' flow if it expects strict ISR
      interruptPending = true;
    }
  }

  int getIrqFlags() {
    if (interruptPending)
      return RADIOLIB_SX126X_IRQ_RX_DONE;
    return 0;
  }

  void clearIrqFlags(int) { interruptPending = false; }

  int readData(String &str) {
    if (rxQueue.empty())
      return 1;
    str = rxQueue.front();
    rxQueue.pop();
    return RADIOLIB_ERR_NONE;
  }

  // Binary Overload
  int readData(uint8_t *data, size_t len) {
    if (binaryRxQueue.empty())
      return 1;
    std::vector<uint8_t> packet = binaryRxQueue.front();
    binaryRxQueue.pop();

    size_t copyLen = (len < packet.size()) ? len : packet.size();
    memcpy(data, packet.data(), copyLen);

    return RADIOLIB_ERR_NONE;
  }

  // Binary Send
  int transmit(uint8_t *data, size_t len) {
    std::cout << "[Radio] Transmitting " << len << " bytes (Binary)\n";
    char hex[8];
    for (size_t i = 0; i < len; i++) {
      // Debug print hex?
    }
    return RADIOLIB_ERR_NONE;
  }
  int transmit(String &str) {
    std::cout << "[Radio] Transmitting: " << str << std::endl;
    return RADIOLIB_ERR_NONE;
  }

  // Test Helpers
  std::string lastTx; // Kept for compatibility with existing tests, though not
                      // used by new transmit
  std::queue<std::string> rxQueue;
  std::queue<std::vector<uint8_t>> binaryRxQueue;
  void (*callback)(void) = nullptr;
  bool interruptPending = false;

  // Helper to simulate receiving a packet
  void mockReceive(std::string data) {
    rxQueue.push(data);
    interruptPending = true; // Set flag
    if (callback)
      callback(); // Trigger ISR
  }

  void mockReceiveBinary(const std::vector<uint8_t> &data) {
    binaryRxQueue.push(data);
    interruptPending = true;
    if (callback)
      callback();
  }

  int getPacketLength() {
    if (!binaryRxQueue.empty())
      return binaryRxQueue.front().size();
    if (!rxQueue.empty()) // Also check string queue for length
      return rxQueue.front().length();
    return 0; // or default size?
  }

  int getRSSI() { return -50; }
  void setCRC(bool) {}
};

extern SX1262 radio; // allow access
