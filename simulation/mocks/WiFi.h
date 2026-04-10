#pragma once
#include "Arduino.h"
#include <iostream>

#define WL_CONNECTED 3

class IPAddress {
public:
  int octets[4];
  IPAddress() {
    octets[0] = 0;
    octets[1] = 0;
    octets[2] = 0;
    octets[3] = 0;
  }
  IPAddress(int a, int b, int c, int d) {
    octets[0] = a;
    octets[1] = b;
    octets[2] = c;
    octets[3] = d;
  }
  String toString() {
    char buf[20];
    sprintf(buf, "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
    return String(buf);
  }
};

class MockWiFi {
public:
  void begin(const char *ssid, const char *pass) {
    std::cout << "[WiFi] Connecting to " << ssid << "...\n";
  }
  int status() { return WL_CONNECTED; }

  bool softAP(const char *ssid, const char *pass = NULL) {
    std::cout << "[WiFi] AP Started: " << ssid << "\n";
    return true;
  }
  IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
};

extern MockWiFi WiFi;
