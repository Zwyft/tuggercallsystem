#pragma once
#include "Arduino.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

static std::map<std::string, std::string> nvs_store;

class Preferences {
  const char *_filename = "nvs.store";

  void load() {
    std::ifstream f(_filename);
    std::string line;
    std::cout << "[MockNVS] Loading from " << _filename << "\n";
    while (std::getline(f, line)) {
      size_t eq = line.find('=');
      if (eq != std::string::npos) {
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Trim CR if present (Windows getline issue?)
        if (!val.empty() && val.back() == '\r')
          val.pop_back();

        nvs_store[key] = val;
        std::cout << "[MockNVS] Loaded key: '" << key << "' = '" << val
                  << "'\n";
      }
    }
  }

  void save() {
    std::ofstream f(_filename);
    for (auto const &[key, val] : nvs_store) {
      f << key << "=" << val << "\n";
    }
  }

public:
  void begin(const char *name, bool readOnly = false) { load(); }
  void end() { save(); }

  void putInt(const char *key, int val) {
    nvs_store[key] = std::to_string(val);
    save();
  }
  int getInt(const char *key, int def = 0) {
    if (nvs_store.count(key))
      return std::stoi(nvs_store[key]);
    return def;
  }

  void putString(const char *key, String val) {
    nvs_store[key] = std::string(val.c_str());
    save();
  }
  String getString(const char *key, String def = "") {
    if (nvs_store.count(key))
      return String(nvs_store[key].c_str());
    return def;
  }

  // Hex helpers for Bytes
  void putBytes(const char *key, const void *buf, size_t len) {
    std::stringstream ss;
    const unsigned char *p = (const unsigned char *)buf;
    for (size_t i = 0; i < len; i++)
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)p[i];
    nvs_store[key] = ss.str();
    save();
  }

  size_t getBytes(const char *key, void *buf, size_t len) {
    if (!nvs_store.count(key))
      return 0;
    std::string hex = nvs_store[key];
    unsigned char *p = (unsigned char *)buf;
    for (size_t i = 0; i < len && (i * 2 + 1) < hex.length(); ++i) {
      std::string byteString = hex.substr(i * 2, 2);
      p[i] = (unsigned char)strtol(byteString.c_str(), NULL, 16);
    }
    return len;
  }

  void remove(const char *key) {
    nvs_store.erase(key);
    save();
  }
};
