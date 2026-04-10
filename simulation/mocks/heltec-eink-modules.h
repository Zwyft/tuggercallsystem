#pragma once
#include "Arduino.h"

class DEPG0290BNS800 {
public:
  void setRotation(int r) { rotation = r; }
  void clear() { buffer = ""; }
  void setCursor(int x, int y) {
    cursorX = x;
    cursorY = y;
  }
  void setTextSize(int s) {}
  void print(String s) { buffer += s; }
  void print(int n) { buffer += std::to_string(n); }
  void print(const char *s) { buffer += s; }

  // Add printf support
  template <typename... Args> void printf(const char *format, Args... args) {
    char buf[256];
    sprintf(buf, format, args...);
    buffer += buf;
  }

  void fillScreen(int color) { buffer = ""; } // Clear on fill
  void begin() {}

  void drawLine(int x0, int y0, int x1, int y1, int c) {
    buffer += "\n[Line]\n";
  }

  void update() {
    std::cout << "\n--- DISPLAY UPDATE ---\n";
    std::cout << buffer << "\n";
    std::cout << "----------------------\n";
  }

  // Test Helpers
  std::string buffer;
  int rotation;
  int cursorX, cursorY;
};

extern DEPG0290BNS800 display;
typedef DEPG0290BNS800 LCMEN2R13EFC1;
