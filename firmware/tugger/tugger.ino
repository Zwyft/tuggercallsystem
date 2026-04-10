#include <Arduino.h>
#include <ArduinoJson.h>
#include <Bounce2.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <SPI.h>
#include <WiFi.h>
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

// ==================== ENUMS & STRUCTS ====================
enum DeviceMode { MODE_TUGGER, MODE_COLLECTOR, MODE_LINE };
enum PacketType {
  PKT_BOOT = 0,
  PKT_ORDER = 1,
  PKT_ACK = 2,
  PKT_CLEAR = 3,
  PKT_DELIVERED = 4
};

struct Order {
  uint32_t orderId;
  char line[32];
  char parts[64];
  int sourceZone;
  uint32_t sourceDeviceId;
  unsigned long createdTime;
  unsigned long expiryTime;
  int status; // 0=Pending, 1=ACK, 2=Completed, 3=Expired
  uint32_t assignedTugger;
};

#pragma pack(push, 1)
struct MeshPacket {
  uint8_t type;
  uint32_t sourceId;
  uint32_t destZone;
  uint32_t orderId;
  uint8_t hopCount;
  char line[28];
  char parts[60];
  uint16_t checksum;
};
#pragma pack(pop)

// ==================== GLOBALS ====================
SPIClass radioSPI(HSPI);
SX1262 radio =
    new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, radioSPI);
DEPG0290BNS800 display; // E290 Driver

Bounce bUp = Bounce();
Bounce bDown = Bounce();
Bounce bSelect = Bounce();

// State
DeviceMode deviceMode = MODE_TUGGER;
int selectedZone = 1;
uint32_t deviceId = 0;

// Order Management
#define MAX_ORDERS 10
Order orders[MAX_ORDERS];
int orderCount = 0;
int selectedIndex = 0;
int scrollOffset = 0;

// Deduplication
#define MAX_SEEN_PACKETS 30
uint32_t seenPackets[MAX_SEEN_PACKETS];
int seenIdx = 0;

// Provisioning / Allow List
uint32_t allowedDevices[10];
int allowedCount = 0;

volatile bool receivedFlag = false;
uint8_t rxBuf[sizeof(MeshPacket)];

// Line Mode Config
String station_name = "LINE STATION";
String btn1_line = "Line 109";
String btn1_part = "109 Pod";
String btn2_line = "Line 112";
String btn2_part = "RH Rails";
// ... can add more

// ==================== UTILS ====================
uint16_t calcChecksum(const void *data, size_t len) {
  uint16_t s = 0;
  const uint8_t *p = (const uint8_t *)data;
  for (size_t i = 0; i < len; i++)
    s += p[i];
  return s;
}

void setFlag(void) { receivedFlag = true; }

bool isPacketSeen(uint32_t id) {
  for (int i = 0; i < MAX_SEEN_PACKETS; i++) {
    if (seenPackets[i] == id)
      return true;
  }
  return false;
}

void markPacketSeen(uint32_t id) {
  seenPackets[seenIdx] = id;
  seenIdx = (seenIdx + 1) % MAX_SEEN_PACKETS;
}

void sendPacket(uint8_t type, uint32_t destZone, uint32_t orderId = 0,
                const char *line = "", const char *parts = "",
                uint8_t hop = 0) {
  MeshPacket p = {0};
  p.type = type;
  p.sourceId = deviceId;
  p.destZone = destZone;
  p.orderId = orderId;
  p.hopCount = hop;
  strncpy(p.line, line, sizeof(p.line) - 1);
  strncpy(p.parts, parts, sizeof(p.parts) - 1);
  p.checksum = calcChecksum(&p, sizeof(p));

  // Serialize to bytes
  uint8_t txBuf[sizeof(MeshPacket)];
  memcpy(txBuf, &p, sizeof(p));
  radio.transmit(txBuf, sizeof(p));
  radio.startReceive();
}

// ==================== DISPLAY ====================
void updateDisplay(String statusMsg = "") {
  display.setRotation(1);
  display.clear();

  // HEADER
  display.setCursor(0, 0);
  display.setTextSize(2);
  if (deviceMode == MODE_TUGGER)
    display.print("TUGGER");
  else if (deviceMode == MODE_COLLECTOR)
    display.print("COLLECTOR");
  else
    display.print("LINE STATION");

  display.setTextSize(1);
  display.setCursor(180, 4);
  display.printf("Zone: %d", selectedZone);
  display.drawLine(0, 20, 250, 20, 0);

  // BODY
  int y = 30;
  if (deviceMode == MODE_TUGGER) {
    if (orderCount == 0) {
      display.setCursor(10, 50);
      display.print("NO ACTIVE ORDERS");
    }
    for (int i = 0; i < orderCount; i++) {
      if (y > 110)
        break;
      display.setCursor(0, y);
      if (i == selectedIndex)
        display.print("> ");
      else
        display.print("  ");

      display.printf("[%d] %s (%s)", orders[i].status, orders[i].parts,
                     orders[i].line);
      y += 14;
    }
  } else if (deviceMode == MODE_LINE) {
    display.setCursor(0, 30);
    display.print("BTN 1: Order Part");
    display.setCursor(0, 50);
    if (statusMsg != "")
      display.print(statusMsg);
  } else {
    display.setCursor(0, 30);
    display.print("Monitoring Mesh...");
  }

  display.update();
}

// ==================== LOGIC ====================
void createOrder(const char *line, const char *parts, int zone) {
  uint32_t oid = (deviceId << 16) | (millis() & 0xFFFF);
  sendPacket(PKT_ORDER, zone, oid, line, parts);
  updateDisplay("Ordered: " + String(parts));
}

void ackSelectedOrder() {
  if (orderCount == 0 || selectedIndex >= orderCount)
    return;
  Order &o = orders[selectedIndex];
  if (o.status != 0)
    return;

  o.status = 1; // ACK
  sendPacket(PKT_ACK, o.sourceZone, o.orderId);
  updateDisplay();
}

void processPacketData(uint8_t *data, size_t len) {
  if (len != sizeof(MeshPacket))
    return;
  MeshPacket *p = (MeshPacket *)data;

  uint16_t crc = p->checksum;
  p->checksum = 0;
  if (crc != calcChecksum(p, sizeof(MeshPacket))) {
    Serial.println("[Mesh] Checksum Fail");
    return;
  }

  if (p->type != PKT_BOOT) {
    if (isPacketSeen(p->orderId)) {
      Serial.printf("[Mesh] Duplicate Packet %lu Ignored.\n", p->orderId);
      return;
    }
    markPacketSeen(p->orderId);
  }

  if (p->type == PKT_ORDER &&
      (p->destZone == selectedZone || p->destZone == 0) &&
      deviceMode == MODE_TUGGER) {
    if (orderCount < MAX_ORDERS) {
      Order &o = orders[orderCount++];
      o.orderId = p->orderId;
      strcpy(o.line, p->line);
      strcpy(o.parts, p->parts);
      o.sourceZone = p->destZone;
      o.status = 0;
      updateDisplay();
    }
    // Send Delivered confirmation
    sendPacket(PKT_DELIVERED, p->destZone, p->orderId);
  } else if (p->type == PKT_ORDER && deviceMode == MODE_COLLECTOR) {
    bool known = false;
    for (int i = 0; i < allowedCount; i++) {
      if (allowedDevices[i] == p->sourceId)
        known = true;
    }
    if (known) {
      Serial.println("PROV:AUTH:" + String(p->sourceId));
      // updateDisplay("COLL: Authorized ID " + String(p->sourceId));
      // delay(1000);
      // updateDisplay("Item: " + String(p->parts));
    } else {
      Serial.println("PROV:BLOCK:" + String(p->sourceId));
      // updateDisplay("COLL: Blocked ID " + String(p->sourceId));
    }
  } else if ((p->type == PKT_ACK || p->type == PKT_CLEAR) &&
             deviceMode == MODE_TUGGER) {
    for (int i = 0; i < orderCount; i++) {
      if (orders[i].orderId == p->orderId) {
        if (p->type == PKT_ACK)
          orders[i].status = 1; // Acked
        else
          orders[i].status = 2; // Cleared (should remove really)
        break;
      }
    }
    updateDisplay();
  }
}

// ==================== SERIAL INTERFACE ====================
void hexStringToBytes(String hex, uint8_t *buffer, int len) {
  for (int i = 0; i < len; i++) {
    char high = hex[i * 2];
    char low = hex[i * 2 + 1];
    uint8_t h = (high >= '0' && high <= '9')   ? high - '0'
                : (high >= 'A' && high <= 'F') ? high - 'A' + 10
                                               : high - 'a' + 10;
    uint8_t l = (low >= '0' && low <= '9')   ? low - '0'
                : (low >= 'A' && low <= 'F') ? low - 'A' + 10
                                             : low - 'a' + 10;
    buffer[i] = (h << 4) | l;
  }
}

void printSysState() {
  DynamicJsonDocument doc(2048);
  doc["mode"] = (int)deviceMode;
  doc["zone"] = selectedZone;
  doc["id"] = deviceId;

  JsonArray ords = doc.createNestedArray("orders");
  for (int i = 0; i < orderCount; i++) {
    JsonObject o = ords.createNestedObject();
    o["id"] = orders[i].orderId;
    o["status"] = orders[i].status;
    o["parts"] = orders[i].parts;
  }

  String output;
  serializeJson(doc, output);
  Serial.println("SYS:STATE:" + output);
}

void onButtonPress(int btnIdx) {
  Serial.printf("BTN:PRESS:%d\n", btnIdx);
  if (btnIdx == 0) { // MODE
    deviceMode = (DeviceMode)((deviceMode + 1) % 3);
    orderCount = 0;
    updateDisplay();
  } else if (btnIdx == 1 && deviceMode == MODE_TUGGER) { // SELECT
    ackSelectedOrder();
  } else if (btnIdx == 2 &&
             deviceMode == MODE_LINE) { // BTN_UP on Line Mode -> Create Order
    createOrder(btn1_line.c_str(), btn1_part.c_str(), selectedZone);
  } else if (btnIdx == 7 && orderCount > 0) { // UP
    if (selectedIndex > 0)
      selectedIndex--;
    updateDisplay();
  } else if (btnIdx == 8 && orderCount > 0) { // DOWN
    if (selectedIndex < orderCount - 1)
      selectedIndex++;
    updateDisplay();
  }
}

void processSerialCommands() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      return;

    if (line.startsWith("BTN:PRESS ")) {
      int idx = line.substring(10).toInt();
      onButtonPress(idx);
      Serial.println("CMD:OK");
    } else if (line.startsWith("MODE:SET ")) {
      int m = line.substring(9).toInt();
      deviceMode = (DeviceMode)m;
      orderCount = 0;
      updateDisplay();
      Serial.println("CMD:OK");
    } else if (line.startsWith("RADIO:INJECT ")) {
      String hex = line.substring(13);
      if (hex.length() == sizeof(MeshPacket) * 2) {
        uint8_t buf[sizeof(MeshPacket)];
        hexStringToBytes(hex, buf, sizeof(MeshPacket));
        Serial.println("CMD:INJECTING");
        processPacketData(buf, sizeof(MeshPacket));
      } else {
        Serial.println("CMD:ERR:LEN");
      }
    } else if (line.startsWith("PROV:ADD ")) {
      uint32_t newId = line.substring(9).toInt();
      if (allowedCount < 10) {
        allowedDevices[allowedCount++] = newId;
        Serial.println("CMD:ADDED");
        updateDisplay("Added ID: " + String(newId));
      } else {
        Serial.println("CMD:ERR:FULL");
      }
    } else if (line == "SYS:STATE") {
      printSysState();
    }
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Booting E290 Hybrid Test Firmware");

  SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);

  bUp.attach(BTN_UP, INPUT_PULLUP);
  bUp.interval(25);
  bDown.attach(BTN_DOWN, INPUT_PULLUP);
  bDown.interval(25);
  bSelect.attach(BTN_SELECT, INPUT_PULLUP);
  bSelect.interval(25);

  // LoRa
  int state =
      radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE,
                  SYNC_WORD, OUTPUT_POWER, PREAMBLE_LENGTH);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("LoRa OK");
    radio.setDio2AsRfSwitch(true);
    radio.setTCXO(1.6);
  } else {
    Serial.printf("LoRa Fail: %d\n", state);
  }
  radio.setDio1Action(setFlag);

  display.begin(); // Init E-Link
  updateDisplay();
  radio.startReceive();

  deviceId = (uint32_t)ESP.getEfuseMac();
}

// ==================== LOOP ====================
void loop() {
  // SERIAL
  processSerialCommands();

  // BUTTONS
  bUp.update();
  bDown.update();
  bSelect.update();
  // Physical Button Map
  // UP (46) -> BTN 7 (Nav Up) or BTN 2 (Line Order 1)
  if (bUp.fell()) {
    if (deviceMode == MODE_LINE)
      onButtonPress(2);
    else
      onButtonPress(7);
  }

  // DOWN (17) -> BTN 8 (Nav Down) or MODE CHANGE (Long press simulation via
  // Serial?) Let's make DOWN toggle mode for convenience in this simple
  // firmware
  if (bDown.fell()) {
    // onButtonPress(0); // Actually let's keep it NAV only, use CLI for Mode
    onButtonPress(8);
  }

  // SELECT (21) -> BTN 1 (Ack)
  if (bSelect.fell())
    onButtonPress(1);

  // RADIO
  if (receivedFlag || radio.getIrqFlags() & RADIOLIB_SX126X_IRQ_RX_DONE) {
    receivedFlag = false;
    String packetStr;
    // Note: E213 used raw bytes, E290 used String readData used in previous
    // file. We switched to raw processing in processPacketData. RadioLib
    // readData(uint8_t* data, size_t len)
    size_t len = radio.getPacketLength();
    if (len == sizeof(MeshPacket)) {
      radio.readData(rxBuf, len);
      processPacketData(rxBuf, len);
    } else {
      radio.startReceive(); // Flush/Ignore invalid
    }
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
    radio.startReceive();
  }
}