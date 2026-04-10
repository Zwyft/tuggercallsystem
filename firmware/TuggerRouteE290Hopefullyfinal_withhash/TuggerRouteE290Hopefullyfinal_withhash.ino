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
#define RF_FREQUENCY 915.0f
#define BANDWIDTH 125.0f
#define SPREADING_FACTOR 9
#define CODING_RATE 7
#define SYNC_WORD 0x12
#define OUTPUT_POWER 14
#define PREAMBLE_LENGTH 8

// Pins for Heltec Wireless Paper V1.1 (ESP32-S3)
#define RADIO_SCLK 9
#define RADIO_MISO 11
#define RADIO_MOSI 10
#define RADIO_NSS 8
#define RADIO_DIO1 14
#define RADIO_RST 12
#define RADIO_BUSY 13

#define BTN_UP 46  
#define BTN_DOWN 17 
#define BTN_SELECT 21 

#define LINE_ID "Line-109-E290"

// Parts Lists
const char *SHIFT_1_PARTS[] = {"109 Pods", "Global Housings", "T1XX Glass", 
                "31XX Glass", "31XX Housing",  "Misc Parts A", "Misc Parts B"};
const int SHIFT_1_COUNT = 7;

const char *SHIFT_2_PARTS[] = {"109 Pods", "Global Housings", "T1XX Glass X2", 
                "31XX Glass X2", "31XX Housing",  "Night Shift Special", "Emergency Kit"};
const int SHIFT_2_COUNT = 7;

// ============================================================
// GLOBALS
// ============================================================
// CHANGE 1: Use global SPI
SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);
DEPG0290BNS800 display;

Bounce bUp = Bounce();
Bounce bDown = Bounce();
Bounce bSelect = Bounce();

int currentShift = 1;
int selectedIndex = 0;
int scrollOffset = 0;

volatile bool receivedFlag = false;

struct PendingOrder {
  String id;
  String item;
  int zone;
  unsigned long lastSent;
  int retries;
  bool active;
};
PendingOrder pending = {"", "", 0, 0, 0, false};

// ============================================================
// HELPER FUNCTIONS
// ============================================================
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void) { receivedFlag = true; }

void updateDisplay(String statusMsg = "") {
  display.setRotation(3); 
  display.clear();
  
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.print(LINE_ID);
  
  display.setTextSize(1);
  display.setCursor(180, 4);
  display.print("Shift: ");
  display.print(currentShift);
  
  display.drawLine(0, 20, 250, 20, 0);
  
  int count = (currentShift == 1) ? SHIFT_1_COUNT : SHIFT_2_COUNT;
  const char **list = (currentShift == 1) ? SHIFT_1_PARTS : SHIFT_2_PARTS;
  
  int y = 30;
  for (int i = scrollOffset; i < count; i++) {
    if (y > 110) break;
    display.setCursor(0, y);
    if (i == selectedIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.print(list[i]);
    y += 14;
  }
  
  if (statusMsg != "") {
    display.drawLine(0, 112, 250, 112, 0);
    display.setCursor(0, 114);
    display.print(statusMsg);
  } else if (pending.active) {
    display.drawLine(0, 112, 250, 112, 0);
    display.setCursor(0, 114);
    display.print("Sending... (");
    display.print(pending.retries);
    display.print(")");
  }
  display.update();
}

void transmitPacket(String type, String id, String item, int zone) {
  JsonDocument doc;
  doc["type"] = type;
  doc["order_id"] = id;
  doc["line"] = LINE_ID;
  doc["zone"] = zone;
  doc["items"] = item;
  doc["timestamp"] = millis();
  String output;
  serializeJson(doc, output);
  
  int state = radio.transmit(output);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("TX OK");
  } else {
    Serial.print("TX FAILED: ");
    Serial.println(state);
  }
  radio.startReceive(); 
}

void sendOrder(String item, int zone) {
  pending.id = String(LINE_ID) + "-" + String(millis(), HEX);
  pending.item = item;
  pending.zone = zone;
  pending.lastSent = 0; 
  pending.retries = 0;
  pending.active = true;
  updateDisplay("Queued: " + item);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // CHANGE 2: Init Global SPI
  SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);

  Serial.println("Booting E290 Line Store - RELIABLE MODE");
  
  bUp.attach(BTN_UP, INPUT_PULLUP);
  bUp.interval(25);
  bDown.attach(BTN_DOWN, INPUT_PULLUP);
  bDown.interval(25);
  bSelect.attach(BTN_SELECT, INPUT_PULLUP);
  bSelect.interval(25);
  
  Serial.print(F("[LoRa] Initializing ... "));
  // Use Global SPI
  int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE,
                          SYNC_WORD, OUTPUT_POWER, PREAMBLE_LENGTH);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
    radio.setDio2AsRfSwitch(true);
    radio.setTCXO(1.6);
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
  }
  radio.setDio1Action(setFlag);
  
  updateDisplay("Ready");
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

  // Retry Logic
  if (pending.active) {
    if (millis() - pending.lastSent > 3000) { 
      pending.lastSent = millis();
      pending.retries++;
      Serial.print("Retrying Order: ");
      Serial.println(pending.id);
      transmitPacket("order", pending.id, pending.item, pending.zone);
      
      // Optional: Only update display every few seconds to prevent SPI conflict
      // updateDisplay(); 
    }
  }

  bUp.update();
  bDown.update();
  bSelect.update();

  if (bUp.fell()) {
    if (selectedIndex > 0) {
      selectedIndex--;
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      updateDisplay();
    }
  }
  if (bDown.fell()) {
    int count = (currentShift == 1) ? SHIFT_1_COUNT : SHIFT_2_COUNT;
    if (selectedIndex < count - 1) {
      selectedIndex++;
      if (selectedIndex >= scrollOffset + 5) scrollOffset++;
      updateDisplay();
    }
  }

  if (bSelect.fell()) {
    int count = (currentShift == 1) ? SHIFT_1_COUNT : SHIFT_2_COUNT;
    const char **list = (currentShift == 1) ? SHIFT_1_PARTS : SHIFT_2_PARTS;
    sendOrder(list[selectedIndex], 1); // Sends to Zone 1
  }

  // LoRa Listen
  if (receivedFlag || radio.getIrqFlags() & RADIOLIB_SX126X_IRQ_RX_DONE) {
    receivedFlag = false;
    String packetStr;
    int state = radio.readData(packetStr);
    if (state == RADIOLIB_ERR_NONE && packetStr.length() > 0) {
      Serial.println("Rx: " + packetStr);
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, packetStr);
      if (!error) {
        const char *type = doc["type"];
        String oid = doc["order_id"].as<String>();
        
        if (strcmp(type, "ack") == 0) {
          if (pending.active && pending.id == oid) {
            pending.active = false; 
            updateDisplay("Tugger: Received!");
            delay(2000);
            updateDisplay("");
          }
        } else if (strcmp(type, "clear") == 0) {
          updateDisplay("Tugger: Cleared!");
          delay(2000);
          updateDisplay("");
        }
      }
    }
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
    radio.startReceive();
  }
}