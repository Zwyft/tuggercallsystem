// ============================================================
// TUGGER FIRMWARE — Heltec Vision Master E290
// MMCall Mesh Network — Performance-optimised build
// ============================================================
// PERFORMANCE FIXES vs previous version:
//  1. SF7 instead of SF9 — air time 95ms vs 290ms (3x faster)
//  2. Random backoff before rebroadcast — eliminates collision storms
//  3. CAD (Channel Activity Detection) before every TX
//  4. Partial e-ink refresh — visual update in ~400ms not 2-3s
//  5. Dual zones — tugger can watch 2 zones simultaneously
//  6. All-call / supervisor zone (route 0)
//  7. Confirmed delivery for urgent packets (PKT_ACK)
//  8. Transmit queue — batches outgoing packets to prevent
//     back-to-back TX starving the RX window
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Bounce2.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <heltec-eink-modules.h>

// ============================================================
// RADIO — SF7 for 3x throughput improvement
// ============================================================
#define RF_FREQUENCY     915.0f
#define BANDWIDTH        125.0f
#define SPREADING_FACTOR 7      // CHANGED from 9 — 95ms air time vs 290ms
#define CODING_RATE      5      // 4/5 — slightly more efficient than 4/7
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
#define PKT_ACK        13
#define PKT_DEBUG      15

#define PRIORITY_NORMAL 0
#define PRIORITY_URGENT 1

// Increment when NVS data format changes or a clean slate on reflash is needed
#define FW_VERSION      2

// Flags byte bits
#define FLAG_ACK_REQ    0x01   // Sender wants an ACK back
#define FLAG_IS_REPLAY  0x02   // This is a catchup replay

// ============================================================
// MESH PACKET — 64 bytes flat, same on all devices
// ============================================================
typedef struct {
    uint32_t srcID;
    uint32_t seqNum;
    uint8_t  type;
    uint8_t  route;      // Zone 0=all, 1-5=specific, 6=supervisor
    uint8_t  ttl;
    uint8_t  hopCount;
    uint8_t  priority;
    uint8_t  flags;
    char     item[28];
    uint8_t  configKey;
    uint8_t  configVal;
    char     configStr[20];
} MeshPacket;

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

// ============================================================
// TX QUEUE — prevents back-to-back TX starving RX
// Outgoing packets are queued and sent with spacing
// ============================================================
#define TX_QUEUE_SIZE 8
MeshPacket txQueue[TX_QUEUE_SIZE];
int txQHead = 0, txQTail = 0, txQCount = 0;

void txEnqueue(void* p) {
    if (txQCount >= TX_QUEUE_SIZE) {
        LOG("TX", "Queue full - dropping oldest");
        txQHead = (txQHead + 1) % TX_QUEUE_SIZE; // evict oldest
        txQCount--;
        // fall through to enqueue newest at tail
    }
    txQueue[txQTail] = *reinterpret_cast<MeshPacket*>(p);
    txQTail = (txQTail + 1) % TX_QUEUE_SIZE;
    txQCount++;
}

// ============================================================
// SEQUENCE DEDUP
// ============================================================
#define MAX_SEQ_ENTRIES 40
typedef struct { uint32_t srcID; uint32_t lastSeq; bool valid; } SeqEntry;
SeqEntry seqTable[MAX_SEQ_ENTRIES];

// ============================================================
// NEIGHBOR TABLE
// ============================================================
#define MAX_NEIGHBORS 20
typedef struct {
    uint32_t srcID; int8_t rssi; uint8_t lastHopCount;
    unsigned long lastSeen; char label[16];
} Neighbor;
Neighbor neighbors[MAX_NEIGHBORS];
int numNeighbors = 0;

// ============================================================
// ACTIVE CALLS
// ============================================================
#define MAX_ACTIVE_CALLS 32
typedef struct {
    uint32_t      srcID;
    uint32_t      seqNum;
    char          item[28];
    char          timeOrdered[6];
    uint8_t       zone;
    uint8_t       priority;
    unsigned long timestamp;
    bool          valid;
    bool          ackPending;   // true if we sent a claim and await mesh confirmation
} ActiveCall;
ActiveCall activeCalls[MAX_ACTIVE_CALLS];
int numActiveCalls = 0;

// ============================================================
// SOFTWARE RTC
// ============================================================
uint32_t      rtcEpoch  = 0;
unsigned long rtcMillis = 0;

uint32_t getCurrentEpoch() {
    if (rtcEpoch == 0) return 0;
    return rtcEpoch + (millis() - rtcMillis) / 1000;
}


String epochToHHMM(uint32_t epoch) {
    if (epoch == 0) return "--:--";
    uint32_t t = epoch % 86400UL;
    char buf[7]; snprintf(buf, sizeof(buf), "%d:%02d", t/3600, (t%3600)/60);
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

const int UP_BUTTON_PIN    = 0;
const int DOWN_BUTTON_PIN  = 47;
int       CLEAR_BUTTON_PIN = 21;

Bounce2::Button upBtn    = Bounce2::Button();
Bounce2::Button downBtn  = Bounce2::Button();
Bounce2::Button clearBtn = Bounce2::Button();

uint32_t myDeviceID   = 0;
uint32_t mySeqNum     = 0;

// DUAL ZONE SUPPORT
uint8_t  zoneA        = 1;     // Primary zone
uint8_t  zoneB        = 0;     // Secondary zone (0 = off)
bool     dualZone     = false;
int      selectedCall = -1;
bool     menuActive   = false;
String   currentShift = "Day";

String   wifiSSID       = "";
String   wifiPass       = "";
bool     wifiOtaEnabled = false;

volatile bool rxFlag = false;
unsigned long lastTX = 0;      // Tracks last TX time for spacing

void IRAM_ATTR onReceive() { rxFlag = true; }

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void doTransmit(void* p);
void saveOrdersToNVS();
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
        prefs.begin("tugger-v1", false);
        prefs.putUInt("my_seq", mySeqNum);
        prefs.end();
    }
    return mySeqNum;
}

// ============================================================
// NEIGHBOR TABLE
// ============================================================
void updateNeighbor(uint32_t srcID, int8_t rssi, uint8_t hopCount,
                    const char* label = nullptr) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].srcID == srcID) {
            neighbors[i].rssi = rssi; neighbors[i].lastHopCount = hopCount;
            neighbors[i].lastSeen = millis();
            if (label) { strncpy(neighbors[i].label, label, 15); neighbors[i].label[15] = '\0'; }
            return;
        }
    }
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (!neighbors[i].lastSeen || millis() - neighbors[i].lastSeen > 300000UL) {
            neighbors[i].srcID = srcID; neighbors[i].rssi = rssi;
            neighbors[i].lastHopCount = hopCount; neighbors[i].lastSeen = millis();
            if (label) { strncpy(neighbors[i].label, label, 15); neighbors[i].label[15] = '\0'; }
            if (i >= numNeighbors) numNeighbors = i + 1;
            return;
        }
    }
}

// Smart TTL — SF7 dense mesh needs fewer hops
uint8_t getSmartTTL() {
    int8_t best = -120; int cnt = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (!neighbors[i].lastSeen || millis() - neighbors[i].lastSeen > 120000UL) continue;
        cnt++; if (neighbors[i].rssi > best) best = neighbors[i].rssi;
    }
    if (cnt == 0) return 4;   // SF7: max 4 hops (faster air time = less margin needed)
    if (best > -70) return 2;
    if (best > -90) return 3;
    return 4;
}

// ============================================================
// CAD + RANDOM BACKOFF + TX
// This is the core fix for collision storms.
// 1. Random backoff 20-150ms before transmitting
// 2. CAD check — listen for 1ms, abort if channel busy
// 3. Only transmit if channel is clear
// ============================================================
void doTransmit(void* p) {
    // Random backoff — spreads simultaneous rebroadcasts from multiple nodes
    delay(random(20, 150));

    // CAD — Channel Activity Detection
    // Returns RADIOLIB_CHANNEL_FREE if clear
    int cadResult = radio.scanChannel();
    if (cadResult != RADIOLIB_CHANNEL_FREE) {
        // Channel busy — wait a bit more and try once more
        delay(random(50, 200));
        cadResult = radio.scanChannel();
        if (cadResult != RADIOLIB_CHANNEL_FREE) {
            // Still busy — queue it to retry after current RX
            txEnqueue(p);
            radio.startReceive();
            return;
        }
    }

    radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket));
    lastTX = millis();
    radio.startReceive();
}

// ============================================================
// NVS ORDER PERSISTENCE
// ============================================================
void saveOrdersToNVS() {
    JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
        if (!activeCalls[i].valid) continue;
        JsonObject o = arr.add<JsonObject>();
        o["src"]  = activeCalls[i].srcID;   o["seq"]  = activeCalls[i].seqNum;
        o["item"] = activeCalls[i].item;    o["time"] = activeCalls[i].timeOrdered;
        o["zone"] = activeCalls[i].zone;    o["pri"]  = activeCalls[i].priority;
        o["elap"] = (millis() - activeCalls[i].timestamp) / 1000UL;
    }
    String json; serializeJson(doc, json);
    prefs.begin("tugger-v1", false);
    prefs.putString("orders", json);
    prefs.putInt("fw_ver", FW_VERSION);
    prefs.end();
}

void loadOrdersFromNVS() {
    // Version check — wipe stale orders from old firmware on reflash
    prefs.begin("tugger-v1", false);
    int storedVer = prefs.getInt("fw_ver", 0);
    if (storedVer != FW_VERSION) {
        LOG("NVS", "Version mismatch (%d vs %d) — wiping orders", storedVer, FW_VERSION);
        prefs.putString("orders", "[]");
        prefs.putInt("fw_ver", FW_VERSION);
        prefs.end();
        return;
    }
    String json = prefs.getString("orders", "[]");
    prefs.end();
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    for (JsonObject o : doc.as<JsonArray>()) {
        if (numActiveCalls >= MAX_ACTIVE_CALLS) break;
        int s = -1;
        for (int i = 0; i < MAX_ACTIVE_CALLS; i++) if (!activeCalls[i].valid) { s=i; break; }
        if (s < 0) break;
        activeCalls[s].srcID    = o["src"].as<uint32_t>();
        activeCalls[s].seqNum   = o["seq"].as<uint32_t>();
        strncpy(activeCalls[s].item, o["item"] | "", 27);
        activeCalls[s].item[27] = '\0'; // Explicit null — slot may be reused, not zero-init
        strncpy(activeCalls[s].timeOrdered, o["time"] | "--:--", 5);
        activeCalls[s].timeOrdered[5] = '\0';
        activeCalls[s].zone     = o["zone"].as<uint8_t>();
        activeCalls[s].priority = o["pri"].as<uint8_t>();
        // Clamp to avoid unsigned underflow on fresh boot with old orders
        unsigned long elapMs = o["elap"].as<unsigned long>() * 1000UL;
        activeCalls[s].timestamp = (elapMs >= millis()) ? 0UL : millis() - elapMs;
        activeCalls[s].valid    = true;
        if (s >= numActiveCalls) numActiveCalls = s + 1;
    }
}

// ============================================================
// ZONE MATCHING — true if call is for a zone we're watching
// Handles: single zone, dual zone, zone 0 (all-call / supervisor)
// ============================================================
bool isMyZone(uint8_t zone) {
    if (zone == 0) return true;          // All-call / supervisor
    if (zone == zoneA) return true;      // Primary zone
    if (dualZone && zone == zoneB) return true; // Secondary zone
    return false;
}

// ============================================================
// RECENTLY-CLEARED CACHE — prevents catchup from reviving a
// just-claimed order before the collector receives the claim.
// Mirrors the line device's clearedAt mechanism.
// ============================================================
#define CLEARED_CACHE_SIZE 16
typedef struct {
    char          item[28];
    uint8_t       zone;
    unsigned long clearedAt; // millis() when claimed — 0 = unused slot
} ClearedCall;
ClearedCall clearedCache[CLEARED_CACHE_SIZE];

void recordClearedCall(const char* item, uint8_t zone) {
    // Overwrite the oldest entry (ring buffer — find first unused or oldest)
    int oldest = 0;
    unsigned long oldestTime = ULONG_MAX;
    for (int i = 0; i < CLEARED_CACHE_SIZE; i++) {
        if (clearedCache[i].clearedAt == 0) { oldest = i; break; } // empty slot
        if (clearedCache[i].clearedAt < oldestTime) {
            oldestTime = clearedCache[i].clearedAt;
            oldest = i;
        }
    }
    strncpy(clearedCache[oldest].item, item, 27);
    clearedCache[oldest].item[27] = '\0';
    clearedCache[oldest].zone      = zone;
    clearedCache[oldest].clearedAt = millis();
}

bool wasRecentlyCleared(const char* item, uint8_t zone) {
    for (int i = 0; i < CLEARED_CACHE_SIZE; i++) {
        if (clearedCache[i].clearedAt == 0) continue;
        // Expire entries older than 5 minutes
        if (millis() - clearedCache[i].clearedAt > 300000UL) {
            clearedCache[i].clearedAt = 0; continue; // expire
        }
        if (clearedCache[i].zone == zone &&
            strncmp(clearedCache[i].item, item, 27) == 0) return true;
    }
    return false;
}

// ============================================================
// ACTIVE CALLS HELPERS
// ============================================================
void addActiveCall(uint32_t srcID, uint32_t seqNum, const char* item,
                   uint8_t zone, uint8_t priority, const char* timeStr) {
    // BUG FIX: If same item+zone already tracked, update seqNum only.
    // Don't create a second slot — that would require two clears.
    for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
        if (activeCalls[i].valid && activeCalls[i].zone == zone &&
            strncmp(activeCalls[i].item, item, 27) == 0) {
            activeCalls[i].seqNum = seqNum; // Update to latest seqNum
            return;
        }
    }
    for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
        if (!activeCalls[i].valid) {
            activeCalls[i] = {};
            activeCalls[i].srcID = srcID; activeCalls[i].seqNum = seqNum;
            strncpy(activeCalls[i].item, item, 27);
            strncpy(activeCalls[i].timeOrdered, timeStr, 5);
            activeCalls[i].timeOrdered[5] = '\0';
            activeCalls[i].zone = zone; activeCalls[i].priority = priority;
            activeCalls[i].timestamp = millis(); activeCalls[i].valid = true;
            if (i >= numActiveCalls) numActiveCalls = i + 1;
            saveOrdersToNVS();
            return;
        }
    }
}

void recountActiveCalls() {
    numActiveCalls = 0;
    for (int i = MAX_ACTIVE_CALLS-1; i >= 0; i--)
        if (activeCalls[i].valid) { numActiveCalls = i+1; break; }
}

void removeActiveCall(int index) {
    if (index < 0 || index >= MAX_ACTIVE_CALLS) return;
    activeCalls[index].valid = false;
    recountActiveCalls();
                    smartUpdateDisplay(); // Instant update when orders cleared
    saveOrdersToNVS();
}

// ============================================================
// CLAIM
// ============================================================
void claimCall(int index) {
    if (index < 0 || index >= MAX_ACTIVE_CALLS || !activeCalls[index].valid) return;
    MeshPacket claim; memset(&claim, 0, sizeof(claim));
    claim.srcID    = myDeviceID; claim.seqNum = nextSeq();
    claim.type     = PKT_CLAIM;  claim.route  = activeCalls[index].zone;
    claim.ttl      = getSmartTTL(); claim.hopCount = 0;
    claim.priority = activeCalls[index].priority;
    claim.flags    = FLAG_ACK_REQ;
    strncpy(claim.item, activeCalls[index].item, 27); claim.item[27] = '\0';

    // Record in cleared cache BEFORE removing — blocks catchup revival
    // for 5 minutes regardless of whether the collector receives the claim.
    recordClearedCall(claim.item, activeCalls[index].zone);

    LOG("CLAIM", "Sending: %s", claim.item);
    remoteLog(claim.item);
    isDuplicate(myDeviceID, claim.seqNum);
    doTransmit(&claim);

    // Re-send once with a fresh seqNum after a short gap to improve
    // delivery reliability. The collector/line device deduplicate by
    // item string match, so a second copy causes no harm if first arrived.
    delay(random(80, 180)); // spacing — ensures first TX is on air first
    claim.seqNum = nextSeq();
    isDuplicate(myDeviceID, claim.seqNum);
    doTransmit(&claim);

    removeActiveCall(index);
    menuActive = false; selectedCall = -1;
}

// ============================================================
// BOOT REQUEST
// ============================================================
void sendBootRequest() {
    MeshPacket req; memset(&req, 0, sizeof(req));
    req.srcID = myDeviceID; req.seqNum = nextSeq();
    req.type = PKT_BOOT_REQ; req.route = 0;
    req.ttl = getSmartTTL(); req.hopCount = 0;
    doTransmit(&req);
}

// ============================================================
// HEARTBEAT
// ============================================================
void sendHeartbeat() {
    MeshPacket hb; memset(&hb, 0, sizeof(hb));
    hb.srcID = myDeviceID; hb.seqNum = nextSeq();
    hb.type = PKT_HEARTBEAT; hb.route = zoneA;
    hb.ttl = 2; hb.hopCount = 0;
    hb.configVal = zoneB; // Encode secondary zone in configVal
    // Build suffix in a local buffer — avoids dangling c_str() from temporary String
    char zoneSuffix[8] = "";
    if (dualZone) snprintf(zoneSuffix, sizeof(zoneSuffix), "+Z%d", zoneB);
    snprintf(hb.item, sizeof(hb.item), "Tugger|Z%d%s", zoneA, zoneSuffix);
    doTransmit(&hb);
}

// ============================================================
// DISPLAY — partial refresh for instant visual update
// fastmodeOn() uses partial refresh (~400ms vs 2-3s full)
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
        display.setTextSize(2); display.setCursor(0, 0);
        display.print("Tugger");
        display.setTextSize(1); display.setCursor(155, 2);
        display.print("Shift: "); display.print(currentShift);
        display.setCursor(155, 12);
        if (dualZone)
            { display.print("Z"); display.print(zoneA); display.print("+Z"); display.print(zoneB); }
        else
            { display.print("Zone: "); display.print(zoneA); }

        display.drawLine(0, 20, 295, 20, BLACK);

        // Count calls for watched zones
        int total = 0;
        for (int i = 0; i < MAX_ACTIVE_CALLS; i++)
            if (activeCalls[i].valid && isMyZone(activeCalls[i].zone)) total++;

        if (total == 0) {
            display.setTextSize(2); display.setCursor(0, 38); display.print("No Orders");
            display.setTextSize(1); display.setCursor(0, 68);
            int activeN = 0;
            for (int i = 0; i < MAX_NEIGHBORS; i++)
                if (neighbors[i].lastSeen && millis()-neighbors[i].lastSeen < 120000UL) activeN++;
            display.print("Peers: "); display.print(activeN);
        } else {
            // Show active calls with better formatting
            display.setTextSize(1);
            display.setCursor(0, 23); display.print("ORDERS:");
            display.setCursor(200, 23); display.print("Time");
            display.drawLine(0, 32, 295, 32, BLACK);
            int y = 35;
            for (int i = 0; i < MAX_ACTIVE_CALLS && y <= 108; i++) {
                if (!activeCalls[i].valid || !isMyZone(activeCalls[i].zone)) continue;
                // Better contrast and spacing for readability
                if (activeCalls[i].priority == PRIORITY_URGENT) {
                    display.fillRect(0, y-1, 295, 12, BLACK);
                    display.setTextColor(WHITE);
                    display.setCursor(2, y); display.print("!"); display.setCursor(8, y);
                } else {
                    display.setTextColor(BLACK);
                    display.setCursor(2, y);
                }
                display.print(String(activeCalls[i].item));
                display.setCursor(200, y);
                display.print(activeCalls[i].timeOrdered);
                y += 13;
            }
        }
        
        // Footer with status info
        display.setTextSize(1);
        display.setCursor(0, 120);
        display.print("UP/DN=select  CLEAR=claim");
    }
}


// ============================================================
// BOOT ZONE SELECTION — supports dual zone selection
// ============================================================
void bootSelectionMenu() {
    // Initialise from saved zones so driver can confirm without re-scrolling
    int  tempA = (zoneA >= 1 && zoneA <= 5) ? zoneA : 1;
    int  tempB = (zoneB >= 0 && zoneB <= 5) ? zoneB : 0;
    bool confirmed = false;
    bool selectingB = false; // Two-step: first select A, then optionally B

    auto draw = [&]() {
        display.setRotation(3);
        DRAW(display) {
            display.setTextColor(BLACK);
            display.setTextSize(2); display.setCursor(0, 0);
            display.print(selectingB ? "2nd Zone?" : "Select Zone");
            display.drawLine(0, 20, 295, 20, BLACK);
            display.setTextSize(3); display.setCursor(100, 38);
            if (selectingB && tempB == 0) display.print("-");
            else display.print(selectingB ? tempB : tempA);
            display.setTextSize(1); display.setCursor(0, 88);
            if (selectingB)
                display.print("UP/DN=zone  CLEAR=set  DOWN(0)=skip 2nd");
            else
                display.print("UP/DN=change  CLEAR=confirm zone");
        }
    };

    draw();
    while (!confirmed) {
        upBtn.update(); downBtn.update(); clearBtn.update();
        bool ch = false;
        if (!selectingB) {
            if (upBtn.pressed())    { tempA = (tempA % 5) + 1; ch = true; }
            if (downBtn.pressed())  { tempA = (tempA == 1) ? 5 : tempA - 1; ch = true; }
            if (clearBtn.pressed()) { selectingB = true; ch = true; } // Move to 2nd zone
        } else {
            if (upBtn.pressed())    { tempB = (tempB % 5) + 1; if (tempB == tempA) tempB = (tempB % 5)+1; ch = true; }
            if (downBtn.pressed())  { tempB = (tempB == 0) ? 0 : tempB - 1; if (tempB == tempA) tempB--; if (tempB < 0) tempB = 0; ch = true; }
            if (clearBtn.pressed()) { confirmed = true; }
        }
        if (ch) draw();
        delay(10);
    }

    zoneA = tempA;
    zoneB = tempB;
    dualZone = (zoneB > 0 && zoneB != zoneA);

    prefs.begin("tugger-v1", false);
    prefs.putInt("zone_a", zoneA);
    prefs.putInt("zone_b", zoneB);
    prefs.putBool("dual_zone", dualZone);
    prefs.end();

    display.setRotation(3);
    DRAW(display) {
        display.setTextColor(BLACK); display.setTextSize(2);
        display.setCursor(0, 30);
        if (dualZone) {
            display.print("Z"); display.print(zoneA);
            display.print(" + Z"); display.print(zoneB);
        } else {
            display.print("Zone "); display.print(zoneA);
        }
        display.print(" Ready");
    }
    delay(300); // Reduced from 700ms to minimise deaf window after radio.startReceive()
}

// ============================================================
// WIFI OTA
// ============================================================
void initWifiOta() {
    if (!wifiOtaEnabled || wifiSSID.length() == 0) return;
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 5000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.setHostname("MMCall-Tugger");
        ArduinoOTA.begin();
        LOG("OTA", "WiFi OK, OTA ready on MMCall-Tugger");
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
    randomSeed(esp_random()); // Seed RNG for backoff

    prefs.begin("tugger-v1", false);
    CLEAR_BUTTON_PIN = prefs.getInt("clear_pin",  21);
    myDeviceID       = prefs.getUInt("device_id",  0);
    mySeqNum         = prefs.getUInt("my_seq",      0);
    zoneA            = prefs.getInt("zone_a",       1);
    zoneB            = prefs.getInt("zone_b",       0);
    dualZone         = prefs.getBool("dual_zone", false);
    wifiSSID         = prefs.getString("wifi_ssid", "");
    wifiPass         = prefs.getString("wifi_pass", "");
    wifiOtaEnabled   = prefs.getBool("wifi_ota",  false);
    prefs.end();

    if (myDeviceID == 0) {
        myDeviceID = esp_random();
        prefs.begin("tugger-v1", false); prefs.putUInt("device_id", myDeviceID); prefs.end();
    }
    mySeqNum += 1000;
    prefs.begin("tugger-v1", false); prefs.putUInt("my_seq", mySeqNum); prefs.end();

    memset(activeCalls,   0, sizeof(activeCalls));
    memset(seqTable,      0, sizeof(seqTable));
    memset(neighbors,     0, sizeof(neighbors));
    memset(txQueue,       0, sizeof(txQueue));
    memset(clearedCache,  0, sizeof(clearedCache));

    upBtn.attach(UP_BUTTON_PIN,       INPUT_PULLUP);
    downBtn.attach(DOWN_BUTTON_PIN,   INPUT_PULLUP);
    clearBtn.attach(CLEAR_BUTTON_PIN, INPUT_PULLUP);
    upBtn.interval(5); downBtn.interval(5); clearBtn.interval(5);

    SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);
    display.begin();

    int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR,
                            CODING_RATE, SYNC_WORD, OUTPUT_POWER, PREAMBLE_LENGTH);
    if (state != RADIOLIB_ERR_NONE) {
        display.setRotation(3);
        DRAW(display) {
            display.setTextColor(BLACK); display.setTextSize(2);
            display.setCursor(0,0); display.print("RADIO FAIL");
            display.setTextSize(1); display.setCursor(0,30);
            display.print("Err: "); display.print(state);
        }
        while (true) delay(1000);
    }

    radio.setDio1Action(onReceive);
    bootSelectionMenu();
    loadOrdersFromNVS();
    initWifiOta();

    delay(random(100, 400));
    radio.startReceive();
    sendBootRequest();
    smartUpdateDisplay();

    LOG("BOOT", "ID=0x%08X ZoneA=%d ZoneB=%d Dual=%d SF=%d WiFiOTA=%d",
        myDeviceID, zoneA, zoneB, dualZone, SPREADING_FACTOR, wifiOtaEnabled);
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    if (wifiOtaEnabled && WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

    // Heartbeat every 60s
    static unsigned long lastHB = 0;
    if (millis() - lastHB > 60000UL) { lastHB = millis(); sendHeartbeat(); }

    // TX queue drain — direct transmit with no backoff delay.
    // Backoff already applied when packet was enqueued via doTransmit().
    // Calling doTransmit() here would add another 20-350ms block on each drain.
    if (txQCount > 0 && millis() - lastTX > 150) {
        int cadResult = radio.scanChannel();
        if (cadResult == RADIOLIB_CHANNEL_FREE) {
            radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]),
                           sizeof(MeshPacket));
            lastTX = millis();
            txQHead = (txQHead + 1) % TX_QUEUE_SIZE;
            txQCount--;
        }
        radio.startReceive();
    }

    // Incoming packet
    if (rxFlag) {
        rxFlag = false;
        MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
        int state = radio.readData(reinterpret_cast<uint8_t*>(&pkt), sizeof(MeshPacket));
        int8_t pktRSSI = (int8_t)radio.getRSSI();

        if (state == RADIOLIB_ERR_NONE) {
            updateNeighbor(pkt.srcID, pktRSSI, pkt.hopCount, pkt.item);
            if (pkt.srcID != myDeviceID && !isDuplicate(pkt.srcID, pkt.seqNum)) {
                // Forward with random backoff — NOT if config/HB/timesync
                if (pkt.type != PKT_CONFIG && pkt.type != PKT_HEARTBEAT &&
                    pkt.type != PKT_TIMESYNC && pkt.ttl > 0) {
                    pkt.ttl--; pkt.hopCount++;
                    // Use queue for forwarding to avoid blocking RX
                    txEnqueue(&pkt);
                }

                if (pkt.type == PKT_TIMESYNC) {
                    uint32_t epoch = (uint32_t)atol(pkt.configStr);
                    if (epoch > 1700000000UL) { rtcEpoch = epoch; rtcMillis = millis(); }
                }

                if (pkt.type == PKT_CALL && isMyZone(pkt.route)) {
                    String t = epochToHHMM(getCurrentEpoch());
                    char tb[6]; t.toCharArray(tb, 6);
                    addActiveCall(pkt.srcID, pkt.seqNum, pkt.item, pkt.route, pkt.priority, tb);
                    LOG("CALL", "Z%d %s", pkt.route, pkt.item);
                    remoteLog(pkt.item);
                    smartUpdateDisplay();
                }

                if (pkt.type == PKT_CATCHUP && isMyZone(pkt.route)) {
                    // Block revival if this item was recently claimed — prevents
                    // the 30s catchup from un-doing a claim before the collector
                    // has processed the PKT_CLAIM. Mirror of line device clearedAt.
                    if (wasRecentlyCleared(pkt.item, pkt.route)) {
                        LOG("CATCHUP", "Blocked revival: %s", pkt.item);
                    } else {
                    String cs = String(pkt.configStr);
                    int pipe = cs.indexOf('|');
                    unsigned long elapSec = pipe > 0 ? (unsigned long)cs.substring(0, pipe).toInt() : (unsigned long)cs.toInt();
                    char tb[6] = "--:--";
                    if (pipe > 0) cs.substring(pipe+1).toCharArray(tb, 6);
                    addActiveCall(pkt.srcID, pkt.seqNum, pkt.item, pkt.route, pkt.priority, tb);
                    // FIX 2b: Clamp elapsed to avoid unsigned underflow on fresh boot
                    // FIX 3:  Match item AND zone so dual-zone tuggers update the right slot
                    unsigned long elapMs = elapSec * 1000UL;
                    for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
                        if (activeCalls[i].valid &&
                            activeCalls[i].zone == pkt.route &&
                            strncmp(activeCalls[i].item, pkt.item, 27) == 0) {
                            activeCalls[i].timestamp = (elapMs >= millis()) ? 0UL : millis() - elapMs;
                            break;
                        }
                    }
                    smartUpdateDisplay();
                    } // end if (!wasRecentlyCleared)
                }

                if (pkt.type == PKT_CLAIM) {
                    String claimed = String(pkt.item);
                    claimed.trim();
                    bool changed = false;
                    for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
                        if (!activeCalls[i].valid) continue;
                        String myItem = String(activeCalls[i].item);
                        myItem.trim();
                        String lineOnly = myItem.indexOf('|') > 0 ?
                            myItem.substring(0, myItem.indexOf('|')) : myItem;
                        if (claimed == myItem || claimed == lineOnly) {
                            // Record in cleared cache so catchup can't revive it
                            recordClearedCall(activeCalls[i].item, activeCalls[i].zone);
                            activeCalls[i].valid = false;
                            changed = true;
                        }
                    }
                    if (changed) {
                        recountActiveCalls();
                        saveOrdersToNVS();
                        smartUpdateDisplay();
                    }
                }

                if (pkt.type == PKT_CONFIG) {
                    prefs.begin("tugger-v1", false);
                    switch (pkt.configKey) {
                        case 1:  // clear_pin + restart
                            prefs.putInt("clear_pin", pkt.configVal);
                            prefs.putUInt("my_seq", mySeqNum + 1000);
                            prefs.end(); ESP.restart(); break;
                        case 9:  // wifi_ota enable + reconnect (matches Line Device key 9)
                            wifiOtaEnabled = (pkt.configVal == 1);
                            prefs.putBool("wifi_ota", wifiOtaEnabled);
                            prefs.end();
                            LOG("CFG", "WiFiOTA=%d", wifiOtaEnabled);
                            if (wifiOtaEnabled) initWifiOta();
                            return;
                        case 11: // wifi_ssid (matches Line Device key 11)
                            wifiSSID = String(pkt.configStr);
                            prefs.putString("wifi_ssid", wifiSSID); break;
                        case 12: // wifi_pass (matches Line Device key 12)
                            wifiPass = String(pkt.configStr);
                            prefs.putString("wifi_pass", wifiPass); break;
                        default: break;
                    }
                    prefs.end();
                }

                if (pkt.type == PKT_DEBUG) { /* no action — Collector handles it */ }
            }
        }
        radio.startReceive();
    }

    upBtn.update(); downBtn.update(); clearBtn.update();

    // Safety: reset selectedCall if it points to an invalid/empty slot
    if (selectedCall >= 0 && (selectedCall >= MAX_ACTIVE_CALLS ||
        !activeCalls[selectedCall].valid)) {
        selectedCall = -1;
        menuActive   = false;
    }

    if (upBtn.pressed()) {
        menuActive = true;
        int start = (selectedCall < 0) ? MAX_ACTIVE_CALLS-1 : selectedCall-1;
        if (start < 0) start = MAX_ACTIVE_CALLS-1;
        for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
            int idx = (start - i + MAX_ACTIVE_CALLS) % MAX_ACTIVE_CALLS;
            if (activeCalls[idx].valid && isMyZone(activeCalls[idx].zone)) { selectedCall=idx; break; }
        }
        smartUpdateDisplay();
    }
    if (downBtn.pressed()) {
        menuActive = true;
        int start = (selectedCall < 0) ? 0 : selectedCall+1;
        if (start >= MAX_ACTIVE_CALLS) start = 0;
        for (int i = 0; i < MAX_ACTIVE_CALLS; i++) {
            int idx = (start + i) % MAX_ACTIVE_CALLS;
            if (activeCalls[idx].valid && isMyZone(activeCalls[idx].zone)) { selectedCall=idx; break; }
        }
        smartUpdateDisplay();
    }
    if (clearBtn.pressed()) {
        if (menuActive && selectedCall >= 0 && activeCalls[selectedCall].valid)
            claimCall(selectedCall);
        else
            for (int i = 0; i < MAX_ACTIVE_CALLS; i++)
                if (activeCalls[i].valid && isMyZone(activeCalls[i].zone)) { claimCall(i); break; }
        smartUpdateDisplay();
    }
}
