#pragma once
#include "Arduino.h"
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Forward declarations
class JsonObject;
class JsonArray;

// Variant Type Enum
enum VariantType { V_NULL, V_STRING, V_INT, V_OBJECT, V_ARRAY };

// Minimal JsonObject/Array support logic
class JsonVariant {
public:
  VariantType type = V_NULL;
  std::string valString;
  int valInt = 0;

  // For recursive structures, using pointers to avoid circular deps
  // In a real mock, we'd use smart pointers or a proper variant implementation
  // simplified: We only hold data. The Document holds the structure.
  // Wait, `createNestedArray` returns a JsonArray.

  // Let's implement a simplified recursive structure using shared_ptr for
  // automatic cleanup
  std::shared_ptr<std::map<std::string, JsonVariant>> objMap;
  std::shared_ptr<std::vector<JsonVariant>> arrVec;

  JsonVariant() { type = V_NULL; }
  JsonVariant(std::string v) : type(V_STRING), valString(v) {}
  JsonVariant(int v) : type(V_INT), valInt(v) {
    valString = std::to_string(v);
  } // Keep string sync for easy printing
  JsonVariant(const char *v) : type(V_STRING), valString(v ? v : "") {}

  // Assignment
  void operator=(const char *v) {
    type = V_STRING;
    valString = v ? v : "";
  }
  void operator=(const std::string &v) {
    type = V_STRING;
    valString = v;
  }
  void operator=(int v) {
    type = V_INT;
    valInt = v;
    valString = std::to_string(v);
  }
  void operator=(uint32_t v) {
    type = V_INT;
    valInt = (int)v;
    valString = std::to_string(v);
  }
  void operator=(unsigned long v) { // unsigned long might be ulong or uint64 on
                                    // some systems, needed for millis()
    type = V_INT;
    valInt = (int)v;
    valString = std::to_string(v);
  }

  void operator=(bool v) {
    type = V_INT;
    valInt = v;
    valString = v ? "true" : "false";
  }

  // Retrieval
  const char *as_char_ptr() const { return valString.c_str(); }
  int as_int() const {
    if (type == V_INT)
      return valInt;
    try {
      return std::stoi(valString);
    } catch (...) {
      return 0;
    }
  }

  // Conversions
  operator const char *() const { return valString.c_str(); }
  operator int() const { return as_int(); }
  operator String() const { return String(valString.c_str()); }

  // Templates
  template <typename T> T as() const;

  // Array/Object Access
  JsonVariant &operator[](const char *key);
  JsonVariant &operator[](int index);
};

// Specialized as<int>
template <> inline int JsonVariant::as<int>() const { return as_int(); }
template <> inline String JsonVariant::as<String>() const {
  return String(valString.c_str());
}

// ================== OBJECT & ARRAY WRAPPERS ==================

class JsonObject {
  std::shared_ptr<std::map<std::string, JsonVariant>> _map;

public:
  JsonObject(std::shared_ptr<std::map<std::string, JsonVariant>> m) : _map(m) {}
  JsonVariant &operator[](const char *key) { return (*_map)[key]; }
  JsonVariant &operator[](String key) {
    return (*_map)[key.c_str()]; // Simplified
  }
};

class JsonArray {
  std::shared_ptr<std::vector<JsonVariant>> _vec;

public:
  JsonArray(std::shared_ptr<std::vector<JsonVariant>> v) : _vec(v) {}

  JsonObject createNestedObject();
  // JsonArray createNestedArray(); // Not needed yet

  void add(JsonVariant v) { _vec->push_back(v); }
};

// ================== IMPLEMENTATION OF NESTING ==================

// We need these defined before usage
inline JsonVariant &JsonVariant::operator[](const char *key) {
  if (type != V_OBJECT) {
    type = V_OBJECT;
    objMap = std::make_shared<std::map<std::string, JsonVariant>>();
  }
  return (*objMap)[key];
}

inline JsonVariant &JsonVariant::operator[](int index) {
  if (type != V_ARRAY) {
    type = V_ARRAY;
    arrVec = std::make_shared<std::vector<JsonVariant>>();
  }
  if (index >= arrVec->size()) {
    arrVec->resize(index + 1);
  }
  return (*arrVec)[index];
}

inline JsonObject JsonArray::createNestedObject() {
  JsonVariant v;
  v.type = V_OBJECT;
  v.objMap = std::make_shared<std::map<std::string, JsonVariant>>();
  _vec->push_back(v);
  return JsonObject(v.objMap);
}

// ================== DOCUMENT ==================

class JsonDocument {
public:
  JsonVariant root;

  // Accessor
  JsonVariant &operator[](const char *key) { return root[key]; }
  JsonVariant &operator[](String key) { return root[key.c_str()]; }

  bool containsKey(const char *key) {
    if (root.type != V_OBJECT || !root.objMap)
      return false;
    return root.objMap->count(key);
  }

  JsonArray createNestedArray(const char *key) {
    JsonVariant &v = root[key];
    v.type = V_ARRAY;
    v.arrVec = std::make_shared<std::vector<JsonVariant>>();
    return JsonArray(v.arrVec);
  }

  JsonObject createNestedObject(const char *key) {
    JsonVariant &v = root[key];
    v.type = V_OBJECT;
    v.objMap = std::make_shared<std::map<std::string, JsonVariant>>();
    return JsonObject(v.objMap);
  }
};

class DynamicJsonDocument : public JsonDocument {
public:
  DynamicJsonDocument(int size) {}
};

enum DeserializationError { Ok, InvalidInput, NoMemory };

// Helper to serialize
inline void serializeVariant(const JsonVariant &v, std::string &out) {
  if (v.type == V_INT) {
    out += std::to_string(v.valInt);
  } else if (v.type == V_STRING) {
    out += "\"" + v.valString + "\"";
  } else if (v.type == V_OBJECT && v.objMap) {
    out += "{";
    bool f = true;
    for (auto const &[key, val] : *v.objMap) {
      if (!f)
        out += ",";
      out += "\"" + key + "\":";
      serializeVariant(val, out);
      f = false;
    }
    out += "}";
  } else if (v.type == V_ARRAY && v.arrVec) {
    out += "[";
    bool f = true;
    for (const auto &val : *v.arrVec) {
      if (!f)
        out += ",";
      serializeVariant(val, out);
      f = false;
    }
    out += "]";
  } else {
    out += "\"\""; // null or empty
  }
}

inline void serializeJson(const JsonDocument &doc, String &output) {
  std::string out = "";
  serializeVariant(doc.root, out);
  output = String(out.c_str());
}

// Simplified Flat Deserializer (Recursive deserialization is hard in single
// header mock) We only really need it for flat configs: {"x":1, "y":2}
inline DeserializationError deserializeJson(JsonDocument &doc, String input) {
  // Reuse previous logic but put into root
  std::string s = input;
  // ... [Same parser logic but assigning to doc.root[key] ] ...
  // Simplified: Just copy the flat string implementation from before

  if (input.length() == 0)
    return DeserializationError::InvalidInput;
  size_t pos = 0;
  while (pos < s.length()) {
    size_t q1 = s.find("\"", pos);
    if (q1 == std::string::npos)
      break;
    size_t q2 = s.find("\"", q1 + 1);
    if (q2 == std::string::npos)
      break;
    std::string key = s.substr(q1 + 1, q2 - q1 - 1);
    size_t col = s.find(":", q2);
    if (col == std::string::npos)
      break;
    size_t valStart = col + 1;
    bool quoted = false;
    while (valStart < s.length() && isspace(s[valStart]))
      valStart++;
    if (s[valStart] == '\"') {
      quoted = true;
      valStart++;
    }
    size_t valEnd;
    if (quoted)
      valEnd = s.find("\"", valStart);
    else
      valEnd = s.find_first_of(",}", valStart);
    if (valEnd == std::string::npos)
      valEnd = s.length();
    std::string val = s.substr(valStart, valEnd - valStart);

    // Assign to root object
    if (doc.root.type != V_OBJECT) {
      doc.root.type = V_OBJECT;
      doc.root.objMap = std::make_shared<std::map<std::string, JsonVariant>>();
    }

    if (!quoted && (isdigit(val[0]) || val[0] == '-')) {
      (*doc.root.objMap)[key] = std::stoi(val);
    } else {
      (*doc.root.objMap)[key] = val;
    }

    if (quoted)
      valEnd++;
    pos = s.find_first_of(",}", valEnd);
    if (pos == std::string::npos)
      break;
    pos++;
  }
  return DeserializationError::Ok;
}
