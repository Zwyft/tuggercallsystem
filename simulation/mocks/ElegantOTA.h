#pragma once
#include "Arduino.h"
#include "WebServer.h"
#include <iostream>


class ElegantOTAClass {
public:
  void begin(WebServer *server) {
    std::cout << "[ElegantOTA] Begin - OTA Server Started at /update\n";
  }

  void loop() {
    // In a real device, this handles the upload process.
    // In sim, we do nothing or just print occasionally?
    // Better to do nothing to keep logs clean.
  }
};

extern ElegantOTAClass ElegantOTA;
inline ElegantOTAClass ElegantOTA;
