#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// Forward Decl
class __FlashStringHelper;

// Mock Types
typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
} esp_reset_reason_t;

// Mock Function backed by file persistence for "Simulated Crash"
#include <fstream>
inline esp_reset_reason_t esp_reset_reason() {
  std::ifstream f("sim_reset_reason.dat");
  if (f.good()) {
    int r;
    f >> r;
    f.close();
    std::remove("sim_reset_reason.dat"); // Clear it so next boot is normal
    std::cout << "[MockESP] Detected Crash Reason from file: " << r << "\n";
    return (esp_reset_reason_t)r;
  }
  return ESP_RST_POWERON;
}

// Mock Constants
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define LOW 0
#define HIGH 1
#define LSBFIRST 0
#define MSBFIRST 1
#define HEX 16
#define DEC 10

// Mock Types
typedef bool boolean;
typedef uint8_t byte;

// Mock String Class (Before Serial)
class MockString : public std::string {
public:
  // using std::string::string; // Inherit constructors causing ambiguity?
  // Let's NOT inherit all constructors if they conflict.
  // Manually define the ones we need.
  MockString() : std::string() {}
  MockString(const char *s) : std::string(s ? s : "") {}
  MockString(const std::string &s) : std::string(s) {}

  // Number conversions
  MockString(int val) : std::string(std::to_string(val)) {}
  MockString(unsigned int val) : std::string(std::to_string(val)) {}
  MockString(long val) : std::string(std::to_string(val)) {}
  MockString(unsigned long val) : std::string(std::to_string(val)) {}

  // Base conversion (HEX)
  MockString(long val, int base) { initMulti((long)val, base); }
  MockString(unsigned long val, int base) {
    initMulti((unsigned long)val, base);
  }
  MockString(int val, int base) { initMulti((long)val, base); }
  MockString(unsigned int val, int base) {
    initMulti((unsigned long)val, base);
  }

private:
  void initMulti(long val, int base) {
    if (base == HEX) {
      char buf[32];
      sprintf(buf, "%lX", val);
      this->assign(buf);
    } else {
      this->assign(std::to_string(val));
    }
  }
  void initMulti(unsigned long val, int base) {
    if (base == HEX) {
      char buf[32];
      sprintf(buf, "%lX", val); // %lX works for unsigned long
      this->assign(buf);
    } else {
      this->assign(std::to_string(val));
    }
  }

public:
  // Arduino compatible substring
  MockString substring(unsigned int beginIndex) const {
    if (beginIndex >= this->length())
      return "";
    return this->substr(beginIndex);
  }

  MockString substring(unsigned int beginIndex, unsigned int endIndex) const {
    if (beginIndex >= this->length())
      return "";
    if (endIndex > this->length())
      endIndex = this->length();
    if (beginIndex > endIndex)
      return "";
    return this->substr(beginIndex, endIndex - beginIndex);
  }

  long toInt() const {
    try {
      return std::stol(*this);
    } catch (...) {
      return 0;
    }
  }
};
using String = MockString;

// Mock Serial (After String)
class MockSerial {
public:
  void begin(long baud) { std::cout << "[Serial] Begin " << baud << std::endl; }
  void println(const char *s) {
    std::cout << "[Serial] " << (s ? s : "(null)") << std::endl;
  }
  void println(String s) { std::cout << "[Serial] " << s << std::endl; }
  void println(int s) { std::cout << "[Serial] " << s << std::endl; }
  void print(const char *s) { std::cout << (s ? s : ""); }
  void print(String s) { std::cout << s; }
  void print(int s) { std::cout << s; }

  template <typename... Args> void printf(const char *format, Args... args) {
    char buf[256];
    sprintf(buf, format, args...);
    std::cout << "[Serial] " << buf; // E213 uses printf for debug lines
  }
  void print(const __FlashStringHelper *s); // Defined below or stubbed
  void println(const __FlashStringHelper *s);
};
extern MockSerial Serial;

// F macro
class __FlashStringHelper {
  // Empty tag type
};
#define F(string_literal)                                                      \
  (reinterpret_cast<const __FlashStringHelper *>(string_literal))

inline std::ostream &operator<<(std::ostream &os,
                                const __FlashStringHelper *s) {
  if (s)
    os << (const char *)s;
  return os;
}

// Implement Serial Helper methods
inline void MockSerial::print(const __FlashStringHelper *s) {
  if (s)
    std::cout << (const char *)s;
}
inline void MockSerial::println(const __FlashStringHelper *s) {
  if (s)
    std::cout << "[Serial] " << (const char *)s << std::endl;
}

// Mock Time
static auto start_time = std::chrono::steady_clock::now();
inline unsigned long millis() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time)
      .count();
}

inline void delay(unsigned long ms) {
  // No-op for fast sim
}

// Mock GPIO
inline std::map<uint8_t, int> mockPinStates;
inline void pinMode(uint8_t pin, uint8_t mode) {}
inline void digitalWrite(uint8_t pin, uint8_t val) { mockPinStates[pin] = val; }
inline int digitalRead(uint8_t pin) {
  if (mockPinStates.count(pin))
    return mockPinStates[pin];
  return HIGH; // Default to HIGH (Pullup)
}

// Mock Random
inline long random(long max) { return 0; }
inline long random(long min, long max) { return min; }

// PROGMEM/ICACHE stubs
#define ICACHE_RAM_ATTR
#define PROGMEM

// FreeRTOS / ESP Stubs
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL(portMUX_TYPE *) {}
inline void portENTER_CRITICAL_ISR(portMUX_TYPE *) {}
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE *) {}

// ESP Helper
class MockESP {
public:
  uint64_t getEfuseMac() { return 0x12345678; }
  void restart() {
    std::cout << "\n[ESP] RESTARTING SYSTEM...\n";
    // In simulation, we can't easily reboot the process, so we just print.
    // Or we could throw an exception to be caught in main to loop?
    // For now, print is sufficient verification.
    // main loop could check a flag?
    exit(0); // Terminate simulation to prove it hit this path.
  }
};
extern MockESP ESP;

// Colors for E213 Display
#define WHITE 1
#define BLACK 0
