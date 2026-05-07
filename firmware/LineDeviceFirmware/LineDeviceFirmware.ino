// ============================================================
// LINE DEVICE FIRMWARE — Heltec Vision Master E290
// MMCall Mesh Network
// ============================================================

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Bounce2.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <heltec-eink-modules.h>

// ============================================================
// RADIO CONFIG
// ============================================================
#define RF_FREQUENCY     915.0f
#define BANDWIDTH        125.0f
#define SPREADING_FACTOR 7      // SF7: 95ms air time vs 290ms at SF9
#define CODING_RATE      5      // 4/5 more efficient than 4/7
#define SYNC_WORD        0x12
#define OUTPUT_POWER     14
#define PREAMBLE_LENGTH  8

#define RADIO_SCLK  9
#define RADIO_MISO  11
#define RADIO_MOSI  10
#define RADIO_NSS   8
#define RADIO_DIO1  14
#define RADIO_RST   12
#define RADIO_BUSY  13

// ============================================================
// PACKET TYPES
// ============================================================
#define PKT_CALL        1
#define PKT_CLAIM       2
#define PKT_CONFIG      4
#define PKT_CATCHUP     9
#define PKT_BOOT_REQ   10
#define PKT_TIMESYNC   11
#define PKT_HEARTBEAT  12
#define PKT_DEBUG      15
#define PKT_OTA_BEGIN   5
#define PKT_OTA_CHUNK   6
#define PKT_OTA_END     7
#define PKT_OTA_ACK     8

#define PRIORITY_NORMAL 0
#define PRIORITY_URGENT 1

// ============================================================
// NVS KEYS
// ============================================================
#define NVS_NS          "line-v1"
#define NVS_LINE_ID     "line_id"
#define NVS_ZONE        "zone"
#define NVS_PARTS       "parts"
#define NVS_UP_PIN      "up_pin"
#define NVS_DOWN_PIN    "down_pin"
#define NVS_CONFIRM_PIN "confirm_pin"
#define NVS_LED_PIN     "led_pin"
#define NVS_BUZZER_PIN  "buzzer_pin"
#define NVS_WIFI_OTA    "wifi_ota"
#define NVS_LORA_OTA    "lora_ota"
#define NVS_WIFI_SSID   "wifi_ssid"
#define NVS_WIFI_PASS   "wifi_pass"
#define NVS_DEVICE_ID   "device_id"
#define NVS_MY_SEQ      "my_seq"
#define NVS_FW_VER      "fw_ver"

// Increment this any time the NVS data format changes or a
// clean slate on reflash is desired. On mismatch, orders are
// wiped and the version is updated.
#define FW_VERSION      3

#define OTA_CHUNK_SIZE  200
#define OTA_TIMEOUT_MS  30000
#define MAX_CALL_TYPES  8
#define MAX_NEIGHBORS   20
#define MAX_SEQ_ENTRIES 30

// ============================================================
// MESH PACKET
// ============================================================
typedef struct {
    uint32_t srcID;
    uint32_t seqNum;
    uint8_t  type;
    uint8_t  route;
    uint8_t  ttl;
    uint8_t  hopCount;
    uint8_t  priority;
    uint8_t  flags;
    char     item[28];
    uint8_t  configKey;
    uint8_t  configVal;
    char     configStr[20];
} MeshPacket;

// For PKT_CONFIG, item[0..3] carries an optional unicast destID (0 = zone broadcast).
inline uint32_t pktGetDest(const MeshPacket* p) { uint32_t id=0; memcpy(&id, p->item, 4); return id; }

typedef struct {
    uint32_t sessionID; uint8_t type; uint32_t chunkIndex;
    uint32_t totalChunks; uint32_t totalSize; uint16_t dataLen;
    uint8_t  data[OTA_CHUNK_SIZE]; uint8_t checksum;
} OtaPacket;

typedef struct {
    char     label[24];
    bool     active;
    uint8_t  priority;
    unsigned long clearedAt; // millis() when last cleared — blocks catchup revival
} CallType;

// ============================================================
// SEQUENCE DEDUP
// ============================================================
typedef struct { uint32_t srcID; uint32_t lastSeq; bool valid; } SeqEntry;
SeqEntry seqTable[MAX_SEQ_ENTRIES];

// ============================================================
// NEIGHBOR TABLE
// ============================================================
typedef struct {
    uint32_t srcID; int8_t rssi; uint8_t lastHopCount; unsigned long lastSeen;
} Neighbor;
Neighbor neighbors[MAX_NEIGHBORS];
int numNeighbors = 0;

// ============================================================
// SOFTWARE RTC
// ============================================================
uint32_t      rtcEpoch  = 0;
unsigned long rtcMillis = 0;

uint32_t getCurrentEpoch() {
    if (rtcEpoch == 0) return 0;
    return rtcEpoch + (millis() - rtcMillis) / 1000;
}


uint8_t  myZone     = 1;

bool isMyZone(uint8_t route) {
    if (route == 0) return true; // zone 0 = all-call / supervisor
    return route == myZone;
}

String epochToHHMM(uint32_t epoch) {
    if (epoch == 0) return "--:--";
    uint32_t t = epoch % 86400UL;
    int h = t / 3600, m = (t % 3600) / 60;
    char buf[7]; snprintf(buf, sizeof(buf), "%d:%02d", h, m);
    return String(buf);
}

// ============================================================
// HARDWARE
// ============================================================

void updateDisplay(); // defined in DISPLAY section below

DEPG0290BNS800 display;
SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);

void initDisplay() {
    display.setRotation(3);
}

void smartUpdateDisplay(bool forceFull = false) {
    updateDisplay();
}
Preferences prefs;

// ============================================================
// GLOBALS
// ============================================================
String   LINE_ID    = "Line ???";
uint32_t myDeviceID = 0;
uint32_t mySeqNum   = 0;

int PIN_UP      = 0;
int PIN_DOWN    = 47;
int PIN_CONFIRM = 21;
int PIN_LED     = -1;
int PIN_BUZZER  = -1;

bool   wifiOtaEnabled = false;
bool   loraOtaEnabled = true;
String wifiSSID       = "";
String wifiPass       = "";

CallType callTypes[MAX_CALL_TYPES];
int  numCallTypes  = 0;
int  selectedCall  = -1;
bool menuActive    = false;
bool anyCallActive = false;

bool          otaInProgress     = false;
uint32_t      otaSessionID      = 0;
uint32_t      otaExpectedChunks = 0;
uint32_t      otaReceivedChunks = 0;
unsigned long otaLastChunkMs    = 0;

volatile bool rxFlag = false;

// ============================================================
// LOG RING BUFFER
// ============================================================
#define LOG_BUF_SIZE 64
struct LogEntry { uint32_t ms; char msg[88]; };
static LogEntry logBuf[LOG_BUF_SIZE];
static int logHead = 0, logCount = 0;

void logWrite(const char* fmt, ...) {
    char tmp[84]; va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    LogEntry& e = logBuf[logHead % LOG_BUF_SIZE];
    e.ms = millis(); strncpy(e.msg, tmp, sizeof(e.msg)-1); e.msg[sizeof(e.msg)-1] = '\0';
    logHead++; if (logCount < LOG_BUF_SIZE) logCount++;
    Serial.println(e.msg);
}
#define LOG(tag, fmt, ...) logWrite("[" tag "] " fmt, ##__VA_ARGS__)

Bounce2::Button upBtn      = Bounce2::Button();
Bounce2::Button downBtn    = Bounce2::Button();
Bounce2::Button confirmBtn = Bounce2::Button();

void IRAM_ATTR onReceive() { rxFlag = true; }

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void transmitMesh(void* p);
void handleConfigPacket(void* raw);
void remoteLog(const char* msg);

// ============================================================
// SEQUENCE DEDUP
// ============================================================

bool isDuplicate(uint32_t srcID, uint32_t seqNum) {
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++) {
        if (!seqTable[i].valid || seqTable[i].srcID != srcID) continue;
        if (seqNum < seqTable[i].lastSeq && (seqTable[i].lastSeq - seqNum) > 500) {
            seqTable[i].lastSeq = seqNum; return false;
        }
        if (seqNum <= seqTable[i].lastSeq) return true;
        seqTable[i].lastSeq = seqNum; return false;
    }
    for (int i = 0; i < MAX_SEQ_ENTRIES; i++)
        if (!seqTable[i].valid) { seqTable[i] = {srcID, seqNum, true}; return false; }
    seqTable[0] = {srcID, seqNum, true}; return false;
}

uint32_t nextSeq() {
    mySeqNum++;
    if (mySeqNum % 100 == 0) {
        prefs.begin(NVS_NS, false); prefs.putUInt(NVS_MY_SEQ, mySeqNum); prefs.end();
    }
    return mySeqNum;
}

// ============================================================
// NEIGHBOR TABLE
// ============================================================
void updateNeighbor(uint32_t srcID, int8_t rssi, uint8_t hopCount) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].srcID == srcID) {
            neighbors[i].rssi = rssi; neighbors[i].lastHopCount = hopCount;
            neighbors[i].lastSeen = millis(); return;
        }
    }
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (!neighbors[i].lastSeen || millis() - neighbors[i].lastSeen > 300000UL) {
            neighbors[i] = {srcID, rssi, hopCount, millis()};
            if (i >= numNeighbors) numNeighbors = i + 1; return;
        }
    }
}

uint8_t getSmartTTL() {
    int8_t best = -120; int cnt = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (!neighbors[i].lastSeen || millis() - neighbors[i].lastSeen > 120000UL) continue;
        cnt++; if (neighbors[i].rssi > best) best = neighbors[i].rssi;
    }
    if (cnt == 0) return 5; if (best > -70) return 2;
    if (best > -90) return 3; if (best > -105) return 4; return 5;
}

// ============================================================
// PARTS JSON
// ============================================================
void parsePartsJson(const String& json) {
    numCallTypes  = 0;
    anyCallActive = false; // Reset so display reflects new parts list accurately
    int i = 0, len = json.length();
    while (i < len && numCallTypes < MAX_CALL_TYPES) {
        int s = json.indexOf('"', i); if (s < 0) break;
        int e = json.indexOf('"', s + 1); if (e < 0) break;
        json.substring(s+1, e).toCharArray(callTypes[numCallTypes].label, 24);
        callTypes[numCallTypes].active    = false;
        callTypes[numCallTypes].priority  = PRIORITY_NORMAL;
        callTypes[numCallTypes].clearedAt = 0; // Reset so new part isn't revival-blocked
        numCallTypes++; i = e + 1;
    }
    if (numCallTypes == 0) { strncpy(callTypes[0].label,"Pickup",23); callTypes[0].active=false; callTypes[0].priority=PRIORITY_NORMAL; callTypes[0].clearedAt=0; numCallTypes=1; }
    selectedCall = 0;
}

String buildPartsJson() {
    String j = "[";
    for (int i = 0; i < numCallTypes; i++) {
        j += "\"" + String(callTypes[i].label) + "\"";
        if (i < numCallTypes - 1) j += ",";
    }
    return j + "]";
}

// ============================================================
// NVS CONFIG
// ============================================================
void loadConfig() {
    prefs.begin(NVS_NS, true);
    LINE_ID        = prefs.getString(NVS_LINE_ID,    "Line ???");
    myZone         = (uint8_t)prefs.getInt(NVS_ZONE, 0); // 0 = not set → boot menu
    myDeviceID     = prefs.getUInt(NVS_DEVICE_ID,    0);
    mySeqNum       = prefs.getUInt(NVS_MY_SEQ,       0);
    PIN_UP         = prefs.getInt(NVS_UP_PIN,         0);
    PIN_DOWN       = prefs.getInt(NVS_DOWN_PIN,       47);
    PIN_CONFIRM    = prefs.getInt(NVS_CONFIRM_PIN,    21);
    PIN_LED        = prefs.getInt(NVS_LED_PIN,        -1);
    PIN_BUZZER     = prefs.getInt(NVS_BUZZER_PIN,     -1);
    wifiOtaEnabled = prefs.getBool(NVS_WIFI_OTA,      false);
    loraOtaEnabled = prefs.getBool(NVS_LORA_OTA,      true);
    wifiSSID       = prefs.getString(NVS_WIFI_SSID,   "");
    wifiPass       = prefs.getString(NVS_WIFI_PASS,   "");
    String parts   = prefs.getString(NVS_PARTS,
        "[\"Pod Pickup\",\"Empty Cart\",\"Maintenance\",\"Supervisor\"]");
    prefs.end();
    if (myDeviceID == 0) {
        myDeviceID = esp_random();
        prefs.begin(NVS_NS, false); prefs.putUInt(NVS_DEVICE_ID, myDeviceID); prefs.end();
    }
    mySeqNum += 1000;
    prefs.begin(NVS_NS, false); prefs.putUInt(NVS_MY_SEQ, mySeqNum); prefs.end();
    parsePartsJson(parts);
}

void saveConfig() {
    prefs.begin(NVS_NS, false);
    prefs.putString(NVS_LINE_ID,   LINE_ID);
    prefs.putInt(NVS_ZONE,         myZone);
    prefs.putInt(NVS_UP_PIN,       PIN_UP);
    prefs.putInt(NVS_DOWN_PIN,     PIN_DOWN);
    prefs.putInt(NVS_CONFIRM_PIN,  PIN_CONFIRM);
    prefs.putInt(NVS_LED_PIN,      PIN_LED);
    prefs.putInt(NVS_BUZZER_PIN,   PIN_BUZZER);
    prefs.putBool(NVS_WIFI_OTA,    wifiOtaEnabled);
    prefs.putBool(NVS_LORA_OTA,    loraOtaEnabled);
    prefs.putString(NVS_WIFI_SSID, wifiSSID);
    prefs.putString(NVS_WIFI_PASS, wifiPass);
    prefs.putString(NVS_PARTS,     buildPartsJson());
    prefs.end();
}

void saveActiveOrders() {
    // Format: "active:0,2|cleared:1:1700000000,3:1700001000"
    // active section = indices of active call types
    // cleared section = index:epochSeconds pairs (cleared within last 10 min)
    String active = "";
    for (int i = 0; i < numCallTypes; i++)
        if (callTypes[i].active) active += String(i) + ",";

    // Save clearedAt as relative seconds-ago so it restores correctly
    String cleared = "";
    for (int i = 0; i < numCallTypes; i++) {
        if (callTypes[i].clearedAt > 0) {
            unsigned long agoSec = (millis() - callTypes[i].clearedAt) / 1000UL;
            if (agoSec < 600) // Only save if still within 10 min window
                cleared += String(i) + ":" + String(agoSec) + ",";
        }
    }

    prefs.begin(NVS_NS, false);
    prefs.putString("active",  active);
    prefs.putString("cleared", cleared);
    prefs.putULong("saved_ms", millis()); // Timestamp so load can detect stale data
    prefs.putInt(NVS_FW_VER, FW_VERSION); // Keep version current
    prefs.end();
}

void clearSavedOrders() {
    // Wipe all saved order state from NVS
    prefs.begin(NVS_NS, false);
    prefs.putString("active",  "");
    prefs.putString("cleared", "");
    prefs.putULong("saved_ms", 0);
    prefs.putInt(NVS_FW_VER, FW_VERSION); // Prevent loadActiveOrders wiping twice
    prefs.end();
    for (int i = 0; i < numCallTypes; i++) {
        callTypes[i].active    = false;
        callTypes[i].clearedAt = 0;
    }
    anyCallActive = false;
    Serial.println("[NVS] Orders cleared");
}

void loadActiveOrders() {
    // ── Step 1: Firmware version check ───────────────────────
    // If stored version doesn't match FW_VERSION, wipe orders.
    // This runs automatically on every reflash.
    // Single prefs session for version check + data read
    prefs.begin(NVS_NS, false); // read-write so we can update version if needed
    int storedVer = prefs.getInt(NVS_FW_VER, 0);
    if (storedVer != FW_VERSION) {
        LOG("NVS", "FW version mismatch (stored=%d current=%d) — wiping orders",
            storedVer, FW_VERSION);
        prefs.putString("active",  "");
        prefs.putString("cleared", "");
        prefs.putULong("saved_ms", 0);
        prefs.putInt(NVS_FW_VER, FW_VERSION);
        prefs.end(); // close and return clean
        return;
    }

    // Version matches — read data using the already-open handle (read-write is fine for reads)
    // ── Step 2: Stale order expiry ────────────────────────────
    unsigned long savedMs = prefs.getULong("saved_ms", 0);
    String active  = prefs.getString("active",  "");
    String cleared = prefs.getString("cleared", "");

    // savedMs of 0 means never saved by this firmware — safe to skip
    if (savedMs > 0) {
        // We can't compare millis() across reboots, but the collector
        // will send a catchup within 30s if orders are genuinely still
        // active. So we only restore if saved within last 30 minutes.
        // Since millis() resets on boot, we use a saved epoch if available.
        // For safety, cap at 30 minutes relative to zero (conservative).
        // If more than 1800 seconds of data, skip — catchup will fill in.
        // (savedMs is the millis() value at save time — not useful cross-boot)
        // DECISION: only restore clearedAt (for the revival block).
        // Active orders will be re-sent by the collector catchup within 30s.
        // This prevents stale ghost orders while still blocking revival.
        active = ""; // Don't restore active — let catchup handle it
    }

    // ── Step 3: Restore clearedAt only ───────────────────────
    int start = 0;
    while (start < (int)cleared.length()) {
        int comma = cleared.indexOf(',', start); if (comma < 0) break;
        String entry = cleared.substring(start, comma);
        int colon = entry.indexOf(':');
        if (colon > 0) {
            int idx = entry.substring(0, colon).toInt();
            unsigned long agoSec = entry.substring(colon + 1).toInt();
            if (idx >= 0 && idx < numCallTypes && agoSec < 600) {
                unsigned long elapMs = agoSec * 1000UL;
                // Clamp: prevents unsigned underflow if device just booted
                // and elapMs > millis(). Without clamp, clearedAt wraps to
                // a huge value and the revival-block comparison wraps again,
                // potentially giving a false "expired" result.
                callTypes[idx].clearedAt = (elapMs >= millis()) ? 1UL : millis() - elapMs;
                // 1UL rather than 0: 0 means "never cleared", 1 means "just cleared"
            }
        }
        start = comma + 1;
    }
    prefs.end();
    Serial.println("[NVS] clearedAt restored (active orders deferred to catchup)");
}

// ============================================================
// DISPLAY
// Line device shows same style as tugger:
//   - Header: LINE_ID | Zone
//   - Active orders list with order time
//   - Idle: READY
//   - Menu: scrollable call type selection
// ============================================================

// Optimized for Heltec Vision Master E290 e-ink display
void updateDisplay() {
    display.setRotation(3);
    static unsigned long lastFullRefresh = 0;
    if (millis() - lastFullRefresh > 30000) {
        display.fastmodeOff();
        lastFullRefresh = millis();
    } else {
        display.fastmodeOn();
    }
    DRAW(display) {
        display.setTextColor(BLACK);

        // Header
        display.setTextSize(2);
        display.setCursor(0, 0);
        display.print(LINE_ID);
        display.setTextSize(1);
        display.setCursor(220, 4);
        display.print("Zone: "); display.print(myZone);
        display.setCursor(250, 12);
        display.print("T:"); display.print(getSmartTTL());
        display.drawLine(0, 20, 295, 20, BLACK);

        if (otaInProgress) {
            display.setTextSize(2); display.setCursor(0, 30); display.print("OTA UPDATE");
            display.setTextSize(1); display.setCursor(0, 55);
            display.print("Chunk: "); display.print(otaReceivedChunks);
            display.print(" / "); display.print(otaExpectedChunks);
        } else if (menuActive) {
            // Scrollable call type selection
            display.setTextSize(1);
            display.setCursor(0, 23); display.print("Select part to order:");
            int y = 33;
            for (int i = 0; i < numCallTypes && y <= 108; i++) {
                bool sel = (i == selectedCall);
                if (sel) {
                    display.fillRect(0, y-1, 295, 12, BLACK);
                    display.setTextColor(WHITE);
                } else { display.setTextColor(BLACK); }
                display.setCursor(4, y);
                if (callTypes[i].active) display.print("* ");
                if (callTypes[i].priority == PRIORITY_URGENT) display.print("! ");
                display.print(String(callTypes[i].label));
                y += 13;
            }
            display.setTextColor(BLACK);
            display.setCursor(0, 120);
            display.print("UP/DN=scroll  CONFIRM=order");
        } else if (anyCallActive) {
            // Show active orders like tugger display
            display.setTextSize(1);
            display.setCursor(0, 23); display.print("ORDERED:");
            display.setCursor(258, 23); display.print("Time");
            display.drawLine(0, 32, 295, 32, BLACK);
            int y = 35;
            for (int i = 0; i < numCallTypes && y <= 108; i++) {
                if (!callTypes[i].active) continue;
                display.setTextColor(BLACK);
                if (callTypes[i].priority == PRIORITY_URGENT) {
                    display.setCursor(0, y); display.print("!");
                }
                display.setCursor(callTypes[i].priority == PRIORITY_URGENT ? 8 : 0, y);
                display.print(String(callTypes[i].label));
                display.setCursor(258, y);
                // Show actual time if available
                display.print(epochToHHMM(getCurrentEpoch()));
                y += 13;
            }
            display.setCursor(0, 120);
            display.print("CONFIRM=new order");
        } else {
            display.setTextSize(2);
            display.setCursor(0, 38); display.print("READY");
            display.setTextSize(1);
            display.setCursor(0, 66); display.print("Press CONFIRM to order");
        }
    }
}


// ============================================================
// RADIO TX
// ============================================================
// TX Queue for backoff
#define TX_QUEUE_SIZE 8
MeshPacket txQueue[TX_QUEUE_SIZE];
int txQHead = 0, txQTail = 0, txQCount = 0;
unsigned long lastTX = 0;

void txEnqueue(void* p) {
    if (txQCount >= TX_QUEUE_SIZE) {
        Serial.println("[TX] Queue full - dropping oldest packet");
        txQHead = (txQHead + 1) % TX_QUEUE_SIZE; // evict oldest
        txQCount--;
        // fall through to enqueue newest at tail
    }
    txQueue[txQTail] = *reinterpret_cast<MeshPacket*>(p);
    txQTail = (txQTail + 1) % TX_QUEUE_SIZE;
    txQCount++;
}


void transmitMesh(void* p) {
    // Random backoff prevents collision storms in dense mesh
    delay(random(20, 120));
    // CAD check
    int cad = radio.scanChannel();
    if (cad != RADIOLIB_CHANNEL_FREE) {
        delay(random(50, 180));
        cad = radio.scanChannel();
        if (cad != RADIOLIB_CHANNEL_FREE) {
            txEnqueue(p); radio.startReceive(); return;
        }
    }
    radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket));
    lastTX = millis();
    radio.startReceive();
}

// ============================================================
// SEND CALL PACKET
// ============================================================
void sendCallPacket(int callIndex) {
    if (callIndex < 0 || callIndex >= numCallTypes) return;

    // ALWAYS transmit on explicit user intent — never gate on callTypes[].active.
    //
    // The previous guard (return early if already active) caused a critical ordering
    // failure: the collector's 30-second PKT_CATCHUP broadcast sets callTypes[].active=true
    // on the line device before the worker even presses CONFIRM. When CONFIRM is pressed,
    // the guard fired and silently swallowed the press — no PKT_CALL was ever transmitted.
    // The line device displayed the order, but the tugger and collector never received it.
    //
    // Re-transmitting is safe: both the collector and tugger deduplicate PKT_CALL by
    // item+zone — if the order already exists they just update the seqNum reference and
    // do not create a second entry. So transmitting on every user press is idempotent.

    MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
    pkt.srcID    = myDeviceID;
    pkt.seqNum   = nextSeq();
    pkt.type     = PKT_CALL;
    pkt.route    = myZone;
    pkt.ttl      = getSmartTTL();
    pkt.hopCount = 0;
    pkt.priority = callTypes[callIndex].priority;
    String desc  = LINE_ID + "|" + String(callTypes[callIndex].label);
    desc.toCharArray(pkt.item, sizeof(pkt.item));
    isDuplicate(myDeviceID, pkt.seqNum); // prevent self-echo
    callTypes[callIndex].active = true;
    anyCallActive = true;
    saveActiveOrders();
    smartUpdateDisplay();
    LOG("CALL", "Sent Z%d %s", myZone, pkt.item);
    remoteLog(pkt.item);
    transmitMesh(&pkt);
    // Re-transmit once for reliability — Collector/Tugger dedup by item+zone
    delay(random(80, 150));
    pkt.seqNum = nextSeq();
    isDuplicate(myDeviceID, pkt.seqNum);
    transmitMesh(&pkt);
    if (PIN_LED >= 0) digitalWrite(PIN_LED, HIGH);
    if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); }
}

// ============================================================
// OTA
// ============================================================
uint8_t xorChecksum(const uint8_t* data, uint16_t len) {
    uint8_t c = 0; for (uint16_t i = 0; i < len; i++) c ^= data[i]; return c;
}

void sendOtaAck(uint32_t sessionID, uint32_t chunkIndex) {
    OtaPacket ack; memset(&ack, 0, sizeof(ack));
    ack.sessionID = sessionID; ack.type = PKT_OTA_ACK; ack.chunkIndex = chunkIndex;
    radio.transmit(reinterpret_cast<uint8_t*>(&ack), sizeof(OtaPacket));
    radio.startReceive();
}

// ============================================================
// HANDLE CONFIG
// ============================================================
void handleConfigPacket(void* raw) {
    MeshPacket* pkt = reinterpret_cast<MeshPacket*>(raw);
    uint32_t destID = pktGetDest(pkt);
    if (destID != 0 && destID != myDeviceID) return;
    bool needRestart = false;
    bool skipSave    = false;
    switch (pkt->configKey) {
        case 1:  LINE_ID     = String(pkt->configStr);                      break;
        case 2:  myZone      = pkt->configVal;                              break;
        case 3:  parsePartsJson(String(pkt->configStr));                    break;
        case 4:  PIN_UP      = pkt->configVal; needRestart = true;          break;
        case 5:  PIN_DOWN    = pkt->configVal; needRestart = true;          break;
        case 6:  PIN_CONFIRM = pkt->configVal; needRestart = true;          break;
        case 7:
            PIN_LED = (pkt->configVal == 255) ? -1 : (int)pkt->configVal;
            if (PIN_LED >= 0) pinMode(PIN_LED, OUTPUT); break;
        case 8:
            PIN_BUZZER = (pkt->configVal == 255) ? -1 : (int)pkt->configVal;
            if (PIN_BUZZER >= 0) pinMode(PIN_BUZZER, OUTPUT); break;
        case 9:  wifiOtaEnabled = (pkt->configVal == 1); needRestart = true; break;
        case 10: loraOtaEnabled = (pkt->configVal == 1);                     break;
        case 11: wifiSSID = String(pkt->configStr);                          break;
        case 12: wifiPass = String(pkt->configStr);                          break;
        case 30: // Append single part name (sent by collector for long lists)
            if (pkt->configStr[0] == '[') {
                // Start-of-list marker — reset the parts list
                for (int i = 0; i < numCallTypes; i++) {
                    callTypes[i] = {};
                }
                numCallTypes  = 0;
                selectedCall  = 0;
                anyCallActive = false; // Reset so display doesn't show blank ORDERED screen
                skipSave = true; // Don't persist the empty list — wait for at least one append
            } else if (numCallTypes < MAX_CALL_TYPES && strlen(pkt->configStr) > 0) {
                strncpy(callTypes[numCallTypes].label, pkt->configStr, 23);
                callTypes[numCallTypes].label[23]   = '\0';
                callTypes[numCallTypes].active       = false;
                callTypes[numCallTypes].priority     = PRIORITY_NORMAL;
                callTypes[numCallTypes].clearedAt    = 0;
                numCallTypes++;
                // skipSave stays false — save each append for power-loss resilience
            }
            break;
        default: return;
    }
    if (!skipSave) saveConfig();
    if (needRestart) {
        prefs.begin(NVS_NS, false);
        prefs.putUInt(NVS_MY_SEQ, mySeqNum + 1000);
        prefs.end();
        delay(300);
        ESP.restart();
    }
    smartUpdateDisplay();
}

// ============================================================
// BOOT ZONE SELECTION
// Line device now has the same zone login as the tugger.
// Zone is saved to NVS. If already set, skip menu.
// Hold UP during boot to force re-selection.
// ============================================================
void bootZoneMenu() {
    int  tempZone = (myZone > 0 && myZone <= 5) ? myZone : 1;
    bool confirmed = false;

    auto draw = [&]() {
        display.setRotation(3);
        DRAW(display) {
            display.setTextColor(BLACK);
            display.setTextSize(2);
            display.setCursor(0, 0);
            display.print("Select Area");
            display.drawLine(0, 20, 295, 20, BLACK);
            display.setTextSize(1);
            display.setCursor(0, 28);
            display.print("Line: "); display.print(LINE_ID);
            display.setTextSize(3);
            display.setCursor(110, 50);
            display.print(tempZone);
            display.setTextSize(1);
            display.setCursor(0, 100);
            display.print("UP/DN=change  CONFIRM=set area");
        }
    };

    draw();
    while (!confirmed) {
        upBtn.update(); downBtn.update(); confirmBtn.update();
        bool ch = false;
        if (upBtn.pressed())      { tempZone = (tempZone % 5) + 1; ch = true; }
        if (downBtn.pressed())    { tempZone = (tempZone == 1) ? 5 : tempZone - 1; ch = true; }
        if (confirmBtn.pressed()) { confirmed = true; }
        if (ch) draw();
        delay(10);
    }
    myZone = tempZone;
    prefs.begin(NVS_NS, false);
    prefs.putInt(NVS_ZONE, myZone);
    prefs.end();

    display.setRotation(3);
    DRAW(display) {
        display.setTextColor(BLACK); display.setTextSize(2);
        display.setCursor(0, 30);
        display.print("Area "); display.print(myZone); display.print(" Set");
        display.setTextSize(1); display.setCursor(0, 60);
        display.print(LINE_ID);
    }
    delay(350);
}

// ============================================================
// BOOT REQUEST + HEARTBEAT
// ============================================================
void sendBootRequest() {
    MeshPacket req; memset(&req, 0, sizeof(req));
    req.srcID = myDeviceID; req.seqNum = nextSeq();
    req.type = PKT_BOOT_REQ; req.route = 0;
    req.ttl = getSmartTTL(); req.hopCount = 0;
    LINE_ID.toCharArray(req.item, sizeof(req.item));
    transmitMesh(&req);
}

void sendHeartbeat() {
    MeshPacket hb; memset(&hb, 0, sizeof(hb));
    hb.srcID = myDeviceID; hb.seqNum = nextSeq();
    hb.type = PKT_HEARTBEAT; hb.route = myZone;
    hb.ttl = 2; hb.hopCount = 0;
    // item = "LineID|Zone" so collector can detect and display this device
    String label = LINE_ID + "|Z" + String(myZone);
    label.toCharArray(hb.item, sizeof(hb.item));
    transmitMesh(&hb);
}

// ============================================================
// INIT HELPERS
// ============================================================
void initButtons() {
    upBtn.attach(PIN_UP, INPUT_PULLUP);
    downBtn.attach(PIN_DOWN, INPUT_PULLUP);
    confirmBtn.attach(PIN_CONFIRM, INPUT_PULLUP);
    upBtn.interval(5); downBtn.interval(5); confirmBtn.interval(5);
}

void initRadio() {
    SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);
    int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR,
                            CODING_RATE, SYNC_WORD, OUTPUT_POWER, PREAMBLE_LENGTH);
    if (state != RADIOLIB_ERR_NONE) {
        display.setRotation(3);
        DRAW(display) {
            display.setTextColor(BLACK); display.setTextSize(2);
            display.setCursor(0,0); display.print("RADIO FAIL");
            display.setTextSize(1); display.setCursor(0,30);
            display.print("Error: "); display.print(state);
        }
        while (true) delay(1000);
    }
    radio.setDio1Action(onReceive);
    radio.startReceive();
}

void initWifiOta() {
    if (!wifiOtaEnabled || wifiSSID.length() == 0) return;
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.setHostname(LINE_ID.c_str()); ArduinoOTA.begin();
        LOG("OTA", "WiFi OK, OTA ready on %s", LINE_ID.c_str());
    } else {
        WiFi.disconnect(true);
        LOG("OTA", "WiFi connect failed");
    }
}

void remoteLog(const char* msg) {
    MeshPacket dbg; memset(&dbg, 0, sizeof(dbg));
    dbg.srcID = myDeviceID; dbg.seqNum = nextSeq();
    dbg.type  = PKT_DEBUG;  dbg.route  = 0; dbg.ttl = 1;
    strncpy(dbg.item, msg, 27); dbg.item[27] = '\0';
    txEnqueue(&dbg);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    randomSeed(esp_random()); // Seed RNG for TX backoff collision avoidance
    memset(seqTable,   0, sizeof(seqTable));
    memset(neighbors,  0, sizeof(neighbors));
    memset(callTypes,  0, sizeof(callTypes)); // Guarantees clearedAt=0, active=false on boot
    memset(txQueue,    0, sizeof(txQueue));   // Prevent stale data in queue on boot

    loadConfig();

    if (PIN_LED    >= 0) { pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW); }
    if (PIN_BUZZER >= 0) { pinMode(PIN_BUZZER, OUTPUT); }

    initButtons();
    display.begin();

    // Zone selection:
    // myZone == 0 means never been set → always show menu
    // Hold UP during boot → force zone re-selection
    // Hold CONFIRM during boot → wipe all saved orders (manual clear)
    upBtn.update();
    confirmBtn.update();
    bool forceMenu    = (myZone == 0) || upBtn.read() == LOW;
    bool forceWipe    = confirmBtn.read() == LOW;

    if (forceWipe) {
        display.setRotation(3);
        DRAW(display) {
            display.setTextColor(BLACK);
            display.setTextSize(2); display.setCursor(0, 0);
            display.print("Clearing...");
            display.setTextSize(1); display.setCursor(0, 30);
            display.print("Release CONFIRM");
        }
        // Wait for button release before wiping
        while (confirmBtn.read() == LOW) { confirmBtn.update(); delay(10); }
        clearSavedOrders();
        Serial.println("[BOOT] Manual order wipe via CONFIRM hold");
    }

    if (forceMenu) bootZoneMenu();

    loadActiveOrders();
    smartUpdateDisplay();

    initRadio();
    initWifiOta();

    if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); }

    sendBootRequest();

    LOG("BOOT", "%s Zone=%d ID=0x%08X", LINE_ID.c_str(), myZone, myDeviceID);
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    if (wifiOtaEnabled && WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

    if (otaInProgress && millis() - otaLastChunkMs > OTA_TIMEOUT_MS) {
        Update.abort(); otaInProgress = false; smartUpdateDisplay();
    }

    static unsigned long lastHB = 0;
    if (millis() - lastHB > 60000UL) { lastHB = millis(); sendHeartbeat(); }

    // TX drain — direct transmit + CAD, no blocking backoff.
    // transmitMesh() carries its own delay(random) which would block
    // button handling and OTA for up to 300ms per cycle.
    if (txQCount > 0 && millis() - lastTX > 150) {
        if (radio.scanChannel() == RADIOLIB_CHANNEL_FREE) {
            radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]),
                           sizeof(MeshPacket));
            lastTX = millis();
            txQHead = (txQHead + 1) % TX_QUEUE_SIZE;
            txQCount--;
        }
        radio.startReceive(); // Always restore receive mode after drain attempt
    }

    if (rxFlag) {
        rxFlag = false;
        uint8_t raw[sizeof(OtaPacket)]; memset(raw, 0, sizeof(raw));
        // FIX: Read full OtaPacket size so OTA chunks are not truncated.
        // We peek at raw[4] for OTA type (OtaPacket layout: sessionID[4]+type[1])
        // vs raw[8] for MeshPacket type (srcID[4]+seqNum[4]+type[1]).
        int state = radio.readData(raw, sizeof(OtaPacket));
        int8_t pktRSSI = (int8_t)radio.getRSSI();

        if (state == RADIOLIB_ERR_NONE) {
            // Check OTA type byte (offset 4) first
            uint8_t otaTypeByte  = raw[4];
            MeshPacket* pkt = reinterpret_cast<MeshPacket*>(raw);
            updateNeighbor(pkt->srcID, pktRSSI, pkt->hopCount);

            // OTA path — use raw byte offset 4 for OTA type detection.
            // Guard: CHUNK and END only valid during an active OTA session.
            // This prevents false detection when a MeshPacket's seqNum byte happens
            // to equal an OTA type constant (e.g. seqNum=1029, byte[0]=5=PKT_OTA_BEGIN).
            bool isOtaPacket = loraOtaEnabled && (
                (otaTypeByte == PKT_OTA_BEGIN && !otaInProgress) ||
                (otaTypeByte == PKT_OTA_CHUNK &&  otaInProgress) ||
                (otaTypeByte == PKT_OTA_END   &&  otaInProgress));
            if (isOtaPacket) {
                OtaPacket* ota = reinterpret_cast<OtaPacket*>(raw);
                switch (ota->type) {
                    case PKT_OTA_BEGIN:
                        if (!Update.begin(ota->totalSize)) break;
                        otaInProgress = true; otaSessionID = ota->sessionID;
                        otaExpectedChunks = ota->totalChunks; otaReceivedChunks = 0;
                        otaLastChunkMs = millis(); smartUpdateDisplay();
                        sendOtaAck(ota->sessionID, 0); break;
                    case PKT_OTA_CHUNK:
                        if (!otaInProgress || ota->sessionID != otaSessionID) break;
                        // Bounds-check dataLen before any read/write operation
                        if (ota->dataLen == 0 || ota->dataLen > OTA_CHUNK_SIZE) {
                            LOG("OTA", "Invalid dataLen=%d, aborting", ota->dataLen);
                            sendOtaAck(ota->sessionID, otaReceivedChunks); break;
                        }
                        if (xorChecksum(ota->data, ota->dataLen) != ota->checksum) {
                            sendOtaAck(ota->sessionID, otaReceivedChunks); break;
                        }
                        Update.write(ota->data, ota->dataLen);
                        otaReceivedChunks++; otaLastChunkMs = millis();
                        sendOtaAck(ota->sessionID, ota->chunkIndex); smartUpdateDisplay(); break;
                    case PKT_OTA_END:
                        if (!otaInProgress || ota->sessionID != otaSessionID) break;
                        if (Update.end(true)) {
                            sendOtaAck(ota->sessionID, otaExpectedChunks);
                            prefs.begin(NVS_NS, false);
                            prefs.putUInt(NVS_MY_SEQ, mySeqNum + 1000);
                            prefs.end();
                            delay(500); ESP.restart();
                        }
                        otaInProgress = false; smartUpdateDisplay(); break;
                }
            }
            // Standard mesh path
            else if (pkt->srcID != myDeviceID && !isDuplicate(pkt->srcID, pkt->seqNum)) {
                bool isUnicastCfg = (pkt->type == PKT_CONFIG && pktGetDest(pkt) != 0);
                if ((pkt->type != PKT_CONFIG || isUnicastCfg) && pkt->type != PKT_HEARTBEAT && pkt->ttl > 0) {
                    pkt->ttl--; pkt->hopCount++; txEnqueue(pkt);
                }

                if (pkt->type == PKT_TIMESYNC) {
                    uint32_t epoch = (uint32_t)atol(pkt->configStr);
                    if (epoch > 1700000000UL) { rtcEpoch = epoch; rtcMillis = millis(); }
                }

                if (pkt->type == PKT_CLAIM) {
                    // BUG FIX: Removed route==myZone guard — route mismatch
                    // was silently dropping valid claims. Item string match
                    // is the authoritative check.
                    String claimed = String(pkt->item);
                    claimed.trim();
                    bool cleared = false;
                    for (int i = 0; i < numCallTypes; i++) {
                        if (!callTypes[i].active) continue;
                        // Build both possible match strings
                        String full  = LINE_ID + "|" + String(callTypes[i].label);
                        String lonly = LINE_ID;
                        String ponly = String(callTypes[i].label);
                        full.trim(); lonly.trim(); ponly.trim();
                        if (claimed == full || claimed == lonly || claimed == ponly) {
                            callTypes[i].active    = false;
                            callTypes[i].clearedAt = millis(); // Block catchup revival
                            cleared = true;
                            LOG("CLAIM", "Cleared: %s", callTypes[i].label);
                            remoteLog(callTypes[i].label);
                        }
                    }
                    if (cleared) {
                        anyCallActive = false;
                        for (int i = 0; i < numCallTypes; i++) if (callTypes[i].active) anyCallActive = true;
                        if (!anyCallActive && PIN_LED >= 0) digitalWrite(PIN_LED, LOW);
                        if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); }
                        saveActiveOrders();
                        menuActive = false;
                        smartUpdateDisplay();
                    }
                }

                if (pkt->type == PKT_CATCHUP && pkt->route == myZone) {
                    // BUG FIX: Catchup was reviving orders that the tugger
                    // had just cleared. Now: if clearedAt is set and less
                    // than 10 minutes ago, ignore the catchup for that item.
                    String catchItem = String(pkt->item);
                    catchItem.trim();
                    bool anyNew = false;
                    for (int i = 0; i < numCallTypes; i++) {
                        String full = LINE_ID + "|" + String(callTypes[i].label);
                        full.trim();
                        if (catchItem == full) {
                            // Skip if recently cleared
                            if (callTypes[i].clearedAt > 0 &&
                                millis() - callTypes[i].clearedAt < 600000UL) {
                                LOG("CATCHUP", "Blocked revival of cleared: %s",
                                    callTypes[i].label);
                                continue;
                            }
                            // Only activate if not already active (prevents double-mark)
                            if (!callTypes[i].active) {
                                callTypes[i].active   = true;
                                callTypes[i].priority = pkt->priority;
                                anyCallActive = true;
                                anyNew = true;
                            }
                        }
                    }
                    if (anyNew) {
                        saveActiveOrders();
                        smartUpdateDisplay();
                    }
                }

                if (pkt->type == PKT_CONFIG && (pkt->route == myZone || pkt->route == 0))
                    handleConfigPacket(pkt);
            }
        }
        radio.startReceive();
    }

    upBtn.update(); downBtn.update(); confirmBtn.update();

    if (upBtn.pressed()) {
        if (numCallTypes == 0) { menuActive = false; smartUpdateDisplay(); }
        else if (!menuActive) { menuActive = true; selectedCall = 0; }
        else selectedCall = (selectedCall == 0) ? numCallTypes - 1 : selectedCall - 1;
        if (numCallTypes > 0) smartUpdateDisplay();
    }
    if (downBtn.pressed()) {
        if (numCallTypes == 0) { menuActive = false; smartUpdateDisplay(); }
        else if (!menuActive) { menuActive = true; selectedCall = 0; }
        else selectedCall = (selectedCall + 1) % numCallTypes;
        if (numCallTypes > 0) smartUpdateDisplay();
    }
    if (confirmBtn.pressed()) {
        if (menuActive) { sendCallPacket(selectedCall); menuActive = false; }
        else            { menuActive = true; selectedCall = 0; }
        smartUpdateDisplay();
    }
}
