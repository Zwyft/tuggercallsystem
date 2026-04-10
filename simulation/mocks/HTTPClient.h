#pragma once
#include "Arduino.h"
#include "ArduinoJson.h"
#include <iostream>


class HTTPClient {
public:
  void begin(String url) { std::cout << "[HTTP] Begin: " << url << "\n"; }
  void addHeader(String key, String val) {}
  int POST(String payload) {
    std::cout << "[HTTP] POST: " << payload << "\n";
    return 200;
  }
  void end() {}
};
