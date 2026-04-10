#pragma once
#include "Arduino.h"

// Bounce2 Mock
class Bounce {
public:
  void attach(int pin, int mode = 0) { this->pin = pin; }
  void interval(int ms) {}
  void update() {
    // Logic to detect state change?
    // In simulation, we set state manually from Test Runner
    if (forcedFell) {
      didFall = true;
      forcedFell = false;
    } else {
      didFall = false;
    }
  }
  bool fell() { return didFall; }

  // Test Helpers
  int pin;
  bool didFall = false;
  bool forcedFell = false;

  void mockPress() { forcedFell = true; }
};

extern Bounce bUp;
extern Bounce bDown;
extern Bounce bSelect;
