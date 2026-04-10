#include <Arduino.h>
#include <ArduinoJson.h>
#include <Bounce2.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <SPI.h>
#include <heltec-eink-modules.h>

// ============================================================
// CONFIGURATION
// ============================================================
// LoRa Configuration
#define RF_FREQUENCY 915.0f
#define BANDWIDTH 125.0f
#define SPREADING_FACTOR 9
#define CODING_RATE 7
#define SYNC_WORD 0x12
#define OUTPUT_POWER 14
#define PREAMBLE_LENGTH 8

// Pins for Heltec Wireless Paper (Shared SPI)
#define RADIO_SCLK 9
#define RADIO_MISO 11
#define RADIO_MOSI 10
#define RADIO_NSS 8
#define RADIO_DIO1 14
#define RADIO_RST 12
#define RADIO_BUSY 13

// Display & Button Pins
#define BUTTON_UP 46
#define BUTTON_DOWN 17
#define BUTTON_SELECT 21

// System Constants
#define MAX_ORDERS 100
#define HASH_TABLE_SIZE 256
#define ORDER_TIMEOUT_MS 2700000 

// ============================================================
// DATA STRUCTURES & GLOBALS
// ============================================================
struct Order {
  String order_id;
  String line;
  int zone;
  String items;
  unsigned long created;
  bool timedOut;
  bool active;
};

// CHANGE 1: Use global SPI, not a custom instance
SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);
Preferences preferences;
Bounce btnUp = Bounce();
Bounce btnDown = Bounce();
Bounce btnSelect = Bounce();
DEPG0290BNS800 display;

// State
int currentZone = 0;
Order orderFIFO[MAX_ORDERS];
int fifoHead = 0;
int fifoTail = 0;
int fifoCount = 0;
Order *hashTable[HASH_TABLE_SIZE];

volatile bool receivedFlag = false;

// ============================================================
// HELPER FUNCTIONS
// ============================================================
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void) { receivedFlag = true; }

unsigned int hashString(String &s) {
  unsigned int hash = 5381;
  for (unsigned int i = 0; i < s.length(); i++) {
    hash = ((hash << 5) + hash) + s.charAt(i);
  }
  return hash % HASH_TABLE_SIZE;
}

void addToHashTable(Order *order) {
  unsigned int index = hashString(order->order_id);
  int start = index;
  while (hashTable[index] != nullptr) {
    if (hashTable[index]->order_id == order->order_id) return;
    index = (index + 1) % HASH_TABLE_SIZE;
    if (index == start) return;
  }
  hashTable[index] = order;
}

void removeFromHashTable(String &id) {
  unsigned int index = hashString(id);
  int start = index;
  while (hashTable[index] != nullptr) {
    if (hashTable[index]->order_id == id) {
      hashTable[index] = nullptr;
      return;
    }
    index = (index + 1) % HASH_TABLE_SIZE;
    if (index == start) return;
  }
}

void updateDisplay(String statusMsg) {
  // E-ink updates are slow, this happens AFTER radio stuff now
  display.setRotation(3);
  display.clear();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.print("ZONE: ");
  display.println(currentZone);
  display.setTextSize(1);
  display.println("----------------");
  display.println(statusMsg);
  display.println("----------------");
  int y = 40;
  int idx = fifoHead;
  for (int i = 0; i < fifoCount; i++) {
    if (orderFIFO[idx].active) {
      if (orderFIFO[idx].timedOut)
        display.print("[!] ");
      else
        display.print("[ ] ");
      display.print(orderFIFO[idx].line);
      display.print(": ");
      display.println(orderFIFO[idx].items);
      y += 10;
      if (y > 120) break;
    }
    idx = (idx + 1) % MAX_ORDERS;
  }
  display.update();
}

void sendLoRaPacket(JsonDocument &doc) {
  String output;
  serializeJson(doc, output);
  int state = radio.transmit(output);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Send failed: ");
    Serial.println(state);
  }
  radio.startReceive();
}

void forwardToCollector(Order *o, String type) {
  JsonDocument doc;
  doc["type"] = type;
  doc["order_id"] = o->order_id;
  doc["line"] = o->line;
  doc["zone"] = o->zone;
  doc["items"] = o->items;
  doc["timestamp"] = o->created;
  doc["forwarded"] = true;
  sendLoRaPacket(doc);
}

void sendAck(String orderId) {
  JsonDocument doc;
  doc["type"] = "ack";
  doc["order_id"] = orderId;
  doc["line"] = "Tugger";
  doc["zone"] = currentZone;
  sendLoRaPacket(doc); // THIS needs to be fast
}

void sendClear(String orderId) {
  JsonDocument doc;
  doc["type"] = "clear";
  doc["order_id"] = orderId;
  doc["line"] = "Tugger";
  doc["zone"] = currentZone;
  sendLoRaPacket(doc);
}

// ============================================================
// NVS HELPER FUNCTIONS
// ============================================================
void saveOrdersToNVS() {
  preferences.putInt("head", fifoHead);
  preferences.putInt("tail", fifoTail);
  preferences.putInt("count", fifoCount);
  for (int i = 0; i < MAX_ORDERS; i++) {
    if (orderFIFO[i].active) {
      String key = "o" + String(i);
      String data = orderFIFO[i].order_id + "|" + orderFIFO[i].line + "|" +
                    String(orderFIFO[i].zone) + "|" + orderFIFO[i].items + "|" +
                    String(orderFIFO[i].created) + "|" +
                    String(orderFIFO[i].timedOut);
      preferences.putString(key.c_str(), data);
    } else {
      String key = "o" + String(i);
      preferences.remove(key.c_str());
    }
  }
}

void loadOrdersFromNVS() {
  fifoHead = preferences.getInt("head", 0);
  fifoTail = preferences.getInt("tail", 0);
  fifoCount = preferences.getInt("count", 0);
  for (int i = 0; i < MAX_ORDERS; i++) {
    String key = "o" + String(i);
    String data = preferences.getString(key.c_str(), "");
    if (data != "") {
      int p1 = data.indexOf('|');
      int p2 = data.indexOf('|', p1 + 1);
      int p3 = data.indexOf('|', p2 + 1);
      int p4 = data.indexOf('|', p3 + 1);
      int p5 = data.indexOf('|', p4 + 1);
      if (p1 != -1 && p5 != -1) {
        orderFIFO[i].order_id = data.substring(0, p1);
        orderFIFO[i].line = data.substring(p1 + 1, p2);
        orderFIFO[i].zone = data.substring(p2 + 1, p3).toInt();
        orderFIFO[i].items = data.substring(p3 + 1, p4);
        orderFIFO[i].created = strtoul(data.substring(p4 + 1, p5).c_str(), NULL, 10);
        orderFIFO[i].timedOut = (data.substring(p5 + 1).toInt() == 1);
        orderFIFO[i].active = true;
        addToHashTable(&orderFIFO[i]);
      }
    } else {
      orderFIFO[i].active = false;
    }
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  
  // CHANGE 2: Initialize Global SPI explicitly for Heltec Board
  SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);

  btnUp.attach(BUTTON_UP, INPUT_PULLUP);
  btnDown.attach(BUTTON_DOWN, INPUT_PULLUP);
  btnSelect.attach(BUTTON_SELECT, INPUT_PULLUP);
  
  preferences.begin("tugger", false);
  currentZone = preferences.getInt("zone", 0);
  loadOrdersFromNVS();

  // Init LoRa
  Serial.print(F("[LoRa] Initializing ... "));
  // Use the global SPI instance passed in constructor
  int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE,
                          SYNC_WORD, OUTPUT_POWER, PREAMBLE_LENGTH);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
    radio.setDio2AsRfSwitch(true);
    radio.setTCXO(1.6);
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }

  radio.setDio1Action(setFlag);

  // Zone Selection
  if (currentZone == 0) {
    bool selected = false;
    int tempZone = 1;
    display.clear(); // Ensure display driver init is okay
    display.setCursor(0, 0);
    display.println("SELECT ZONE");
    display.update();
    
    while (!selected) {
      btnUp.update();
      btnSelect.update();
      if (btnUp.fell()) {
        tempZone++;
        if (tempZone > 5) tempZone = 1;
        display.clear();
        display.setCursor(0, 0);
        display.print("SELECT ZONE: ");
        display.println(tempZone);
        display.update();
      }
      if (btnSelect.fell()) {
        currentZone = tempZone;
        preferences.putInt("zone", currentZone);
        selected = true;
        updateDisplay("Zone " + String(currentZone) + " Locked");
      }
      delay(10);
    }
  } else {
    updateDisplay("Zone " + String(currentZone) + " Active");
  }

  radio.startReceive();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 2000) {
    lastHeartbeat = millis();
    Serial.print(".");
  }

  // Check LoRa
  if (receivedFlag || radio.getIrqFlags() & RADIOLIB_SX126X_IRQ_RX_DONE) {
    receivedFlag = false;
    String packetStr;
    int state = radio.readData(packetStr);
    
    if (state == RADIOLIB_ERR_NONE && packetStr.length() > 0) {
      Serial.println("\nReceived: " + packetStr);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, packetStr);
      
      if (!error) {
        const char *type = doc["type"];
        int zone = doc["zone"];
        
        if (strcmp(type, "order") == 0) {
          if (zone == currentZone) {
            if (fifoCount < MAX_ORDERS) {
              String oid = doc["order_id"].as<String>();
              
              // CHANGE 3: Send ACK IMMEDIATELY before processing/display logic
              // This ensures the sender gets the ACK before timing out
              sendAck(oid); 
              
              bool exists = false;
              unsigned int idx = hashString(oid);
              int start = idx;
              while (hashTable[idx] != nullptr) {
                if (hashTable[idx]->order_id == oid) {
                  exists = true;
                  break;
                }
                idx = (idx + 1) % HASH_TABLE_SIZE;
                if (idx == start) break;
              }
              
              if (!exists) {
                Order *newOrder = &orderFIFO[fifoTail];
                newOrder->order_id = oid;
                newOrder->line = doc["line"].as<String>();
                newOrder->zone = zone;
                newOrder->items = doc["items"].as<String>();
                newOrder->created = millis();
                newOrder->timedOut = false;
                newOrder->active = true;
                
                addToHashTable(newOrder);
                fifoTail = (fifoTail + 1) % MAX_ORDERS;
                fifoCount++;
                
                saveOrdersToNVS();
                
                // Update Display AFTER Ack
                updateDisplay("New Order: " + newOrder->items);
                forwardToCollector(newOrder, "order");
              } else {
                Serial.println("Duplicate Order");
                // Ack already sent above
              }
            }
          } 
        }
      }
    }
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
    radio.startReceive();
  }

  // Check Timeouts
  unsigned long now = millis();
  bool changed = false;
  int idx = fifoHead;
  for (int i = 0; i < fifoCount; i++) {
    if (orderFIFO[idx].active && !orderFIFO[idx].timedOut) {
      if (now - orderFIFO[idx].created > ORDER_TIMEOUT_MS) {
        orderFIFO[idx].timedOut = true;
        forwardToCollector(&orderFIFO[idx], "timeout");
        changed = true;
        saveOrdersToNVS();
      }
    }
    idx = (idx + 1) % MAX_ORDERS;
  }
  if (changed) updateDisplay("Timeouts Updated");

  // Button Handling
  btnSelect.update();
  if (btnSelect.fell()) {
    if (fifoCount > 0) {
      if (orderFIFO[fifoHead].active) {
        sendClear(orderFIFO[fifoHead].order_id);
        forwardToCollector(&orderFIFO[fifoHead], "clear");
        removeFromHashTable(orderFIFO[fifoHead].order_id);
        orderFIFO[fifoHead].active = false;
        fifoHead = (fifoHead + 1) % MAX_ORDERS;
        fifoCount--;
        saveOrdersToNVS();
        updateDisplay("Order Cleared");
      }
    }
  }
}