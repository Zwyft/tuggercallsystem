// ============================================================
// COLLECTOR FIRMWARE — Heltec Vision Master E290
// MMCall Mesh Network — v1.1
// ============================================================

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <heltec-eink-modules.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>

// ============================================================
// RADIO CONFIG
// ============================================================
#define RF_FREQUENCY     915.0f
#define BANDWIDTH        125.0f
#define SPREADING_FACTOR 7      // SF7: 3x throughput vs SF9
#define CODING_RATE      5
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

#define PRIORITY_NORMAL 0
#define PRIORITY_URGENT 1

// Increment when NVS data format changes or clean slate on reflash is needed
#define FW_VERSION      1

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

// ============================================================
// SEQUENCE DEDUP
// ============================================================
#define MAX_SEQ_ENTRIES 60
typedef struct { uint32_t srcID; uint32_t lastSeq; bool valid; } SeqEntry;
SeqEntry seqTable[MAX_SEQ_ENTRIES];

// ============================================================
// NEIGHBOR / DEVICE DETECTION TABLE
// Auto-populated from heartbeat packets — no manual pairing needed.
// ============================================================
#define MAX_NEIGHBORS 40
typedef struct {
    uint32_t      srcID;
    int8_t        rssi;
    uint8_t       lastHopCount;
    unsigned long lastSeen;
    char          label[28];   // "Line 112|Z1" from heartbeat item
    uint8_t       zone;
    bool          isLineDevice; // true if heartbeat came from line device
} Neighbor;
Neighbor neighbors[MAX_NEIGHBORS];
int numNeighbors = 0;

// ============================================================
// ACTIVE ORDERS — stores wall-clock time of order
// ============================================================
#define MAX_ACTIVE_ORDERS 64
typedef struct {
    uint32_t      srcID;
    uint32_t      seqNum;
    char          lineID[24];
    char          part[24];
    char          timeOrdered[6];
    unsigned long timestamp;
    unsigned long claimedAt;  // millis() when claimed — prevents catchup revival
    uint8_t       zone;
    uint8_t       priority;
    bool          claimed;
    bool          timedOut;
} ActiveOrder;
ActiveOrder activeOrders[MAX_ACTIVE_ORDERS];
int numActiveOrders = 0;
int selectedCall = -1;   // Ensure invalid index on boot

// Order history for shift report
#define MAX_HISTORY 128
typedef struct {
    char     lineID[24]; char part[24];
    char     timeOrdered[6]; uint32_t elapsedSec;
    uint8_t  zone; bool claimed;
} HistoryEntry;
HistoryEntry history[MAX_HISTORY];
int numHistory = 0;

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

const char* AP_SSID    = "MMCall-Collector";
const char* AP_PASS    = "mmcall123";
const char* NTP_SERVER = "pool.ntp.org";

WebServer server(80);
WiFiUDP   ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, 0, 3600000); // sync every hour

uint32_t myDeviceID = 0;
uint32_t mySeqNum   = 0;
bool     ntpSynced  = false;

volatile bool rxFlag = false;

void IRAM_ATTR onReceive() { rxFlag = true; }

// ============================================================
// LOG RING BUFFER
// ============================================================
#define LOG_BUF_SIZE 128
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
// FORWARD DECLARATIONS
// ============================================================
void transmitMesh(void* p);
void broadcastCatchup();

// ============================================================
// TIME HELPERS
// ============================================================

bool isMyZone(uint8_t route) {
    return true; // Collector is the hub — it stores orders for all zones
}

String epochToHHMM(uint32_t epoch) {
    if (epoch == 0) return "--:--";
    uint32_t t = epoch % 86400UL;
    int h = t / 3600, m = (t % 3600) / 60;
    char buf[7]; snprintf(buf, sizeof(buf), "%d:%02d", h, m);
    return String(buf);
}

uint32_t getCurrentEpoch() {
    if (!ntpSynced) return 0;
    return (uint32_t)timeClient.getEpochTime();
}

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
        prefs.begin("collector", false); prefs.putUInt("my_seq", mySeqNum); prefs.end();
    }
    return mySeqNum;
}

// ============================================================
// NEIGHBOR TABLE — auto-detects all devices from heartbeats
// ============================================================
void updateNeighbor(uint32_t srcID, int8_t rssi, uint8_t hopCount,
                    const char* label, uint8_t zone, bool isLine) {
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (neighbors[i].srcID == srcID) {
            neighbors[i].rssi        = rssi;
            neighbors[i].lastHopCount = hopCount;
            neighbors[i].lastSeen    = millis();
            if (label && strlen(label) > 0) {
                strncpy(neighbors[i].label, label, 27);
                neighbors[i].label[27] = '\0';
            }
            neighbors[i].zone       = zone;
            neighbors[i].isLineDevice = isLine;
            return;
        }
    }
    for (int i = 0; i < MAX_NEIGHBORS; i++) {
        if (!neighbors[i].lastSeen || millis() - neighbors[i].lastSeen > 300000UL) {
            neighbors[i].srcID        = srcID;
            neighbors[i].rssi         = rssi;
            neighbors[i].lastHopCount  = hopCount;
            neighbors[i].lastSeen     = millis();
            if (label) {
                strncpy(neighbors[i].label, label, 27);
                neighbors[i].label[27] = '\0';
            }
            neighbors[i].zone         = zone;
            neighbors[i].isLineDevice = isLine;
            if (i >= numNeighbors) numNeighbors = i + 1;
            return;
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
// NVS PERSISTENCE
// ============================================================
void saveOrdersToNVS() {
    JsonDocument doc; JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < numActiveOrders; i++) {
        if (activeOrders[i].claimed || activeOrders[i].timedOut) continue;
        JsonObject o = arr.add<JsonObject>();
        o["src"]  = activeOrders[i].srcID;   o["seq"]  = activeOrders[i].seqNum;
        o["line"] = activeOrders[i].lineID;  o["part"] = activeOrders[i].part;
        o["time"] = activeOrders[i].timeOrdered;
        o["zone"] = activeOrders[i].zone;    o["pri"]  = activeOrders[i].priority;
        o["elap"] = (millis() - activeOrders[i].timestamp) / 1000UL;
    }
    String json; serializeJson(doc, json);
    prefs.begin("collector", false);
    prefs.putString("orders", json);
    prefs.putInt("fw_ver", FW_VERSION);
    prefs.end();
}

void loadOrdersFromNVS() {
    // Version check — wipe stale orders from old firmware on reflash
    prefs.begin("collector", false);
    int storedVer = prefs.getInt("fw_ver", 0);
    bool versionOk = (storedVer == FW_VERSION);
    if (!versionOk) {
        LOG("NVS", "Version mismatch (%d vs %d) — wiping orders", storedVer, FW_VERSION);
        prefs.putString("orders", "[]");
        prefs.putInt("fw_ver", FW_VERSION);
    }
    String json = versionOk ? prefs.getString("orders", "[]") : "[]";
    prefs.end(); // Single close point for both paths
    if (!versionOk) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    for (JsonObject o : doc.as<JsonArray>()) {
        if (numActiveOrders >= MAX_ACTIVE_ORDERS) break;
        int idx = numActiveOrders; // Increment AFTER slot fully written
        activeOrders[idx] = {};    // Zero-init slot before populating
        activeOrders[idx].srcID    = o["src"].as<uint32_t>();
        activeOrders[idx].seqNum   = o["seq"].as<uint32_t>();
        strncpy(activeOrders[idx].lineID, o["line"] | "", 23);
        activeOrders[idx].lineID[23] = '\0';
        strncpy(activeOrders[idx].part,   o["part"] | "", 23);
        activeOrders[idx].part[23]   = '\0';
        strncpy(activeOrders[idx].timeOrdered, o["time"] | "--:--", 5);
        activeOrders[idx].timeOrdered[5] = '\0';
        activeOrders[idx].zone     = o["zone"].as<uint8_t>();
        activeOrders[idx].priority = o["pri"].as<uint8_t>();
        unsigned long elap   = o["elap"].as<unsigned long>();
        unsigned long elapMs = elap * 1000UL;
        // Clamp: prevents unsigned underflow if order is older than device uptime
        activeOrders[idx].timestamp = (elapMs >= millis()) ? 0UL : millis() - elapMs;
        activeOrders[idx].claimed  = false;
        activeOrders[idx].timedOut = false;
        numActiveOrders++; // Increment only after slot is fully populated
    }
    LOG("NVS", "Loaded %d orders", numActiveOrders);
}

// ============================================================
// RADIO TX
// ============================================================
#define TX_QUEUE_SIZE 16  // Collector needs larger queue
MeshPacket txQueue[TX_QUEUE_SIZE];
int txQHead = 0, txQTail = 0, txQCount = 0;
unsigned long lastTX = 0;

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


void transmitMesh(void* p) {
    delay(random(20, 120));
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
// TIME SYNC BROADCAST — sends NTP time to all devices
// ============================================================
void broadcastTimeSync() {
    if (!ntpSynced) return;
    MeshPacket ts; memset(&ts, 0, sizeof(ts));
    ts.srcID    = myDeviceID; ts.seqNum = nextSeq();
    ts.type     = PKT_TIMESYNC; ts.route = 0;
    ts.ttl      = getSmartTTL(); ts.hopCount = 0;
    uint32_t epoch = (uint32_t)timeClient.getEpochTime();
    snprintf(ts.configStr, sizeof(ts.configStr), "%lu", (unsigned long)epoch);
    isDuplicate(myDeviceID, ts.seqNum);
    transmitMesh(&ts);
    LOG("NTP", "Broadcast epoch=%lu", (unsigned long)epoch);
}

// ============================================================
// CATCHUP BROADCAST — replay all active orders
// configStr carries "elapsedSec|HH:MM" so receivers restore timer + time
// ============================================================
void broadcastCatchup() {
    int sent = 0;
    for (int i = 0; i < numActiveOrders; i++) {
        if (activeOrders[i].claimed || activeOrders[i].timedOut) continue;
        // NOTE: No additional claimedAt guard here — it was unreachable dead code.
        // claimed=true and claimedAt are always set together, so the above check
        // is sufficient. Revival protection on the tugger side (clearedCache) handles
        // the race window between claim transmission and collector receipt.
        MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
        pkt.srcID    = myDeviceID; pkt.seqNum = nextSeq();
        pkt.type     = PKT_CATCHUP; pkt.route = activeOrders[i].zone;
        pkt.ttl      = getSmartTTL(); pkt.hopCount = 0;
        pkt.priority = activeOrders[i].priority; pkt.flags = 0x02;
        String item  = String(activeOrders[i].lineID) + "|" + String(activeOrders[i].part);
        item.toCharArray(pkt.item, sizeof(pkt.item));
        unsigned long elapSec = (millis() - activeOrders[i].timestamp) / 1000UL;
        // configStr = "elapsed|HH:MM"
        snprintf(pkt.configStr, sizeof(pkt.configStr), "%lu|%s",
                 (unsigned long)elapSec, activeOrders[i].timeOrdered);
        isDuplicate(myDeviceID, pkt.seqNum);
        // Direct transmit — inter-packet delay below handles spacing.
        // transmitMesh() would add another 20-120ms random delay per packet.
        if (radio.scanChannel() == RADIOLIB_CHANNEL_FREE) {
            radio.transmit(reinterpret_cast<uint8_t*>(&pkt), sizeof(MeshPacket));
            lastTX = millis();
        }
        radio.startReceive();
        sent++;
        delay(random(60, 120)); // Inter-packet spacing
    }
    LOG("CATCHUP", "Sent %d orders", sent);
}

// Numeric overload — used for configKey 2 (zone), 4-8 (pins), 9-10 (OTA flags).
// Line device reads configVal for these keys; configStr stays zeroed.
void sendConfigPacket(uint8_t zone, uint8_t key, uint8_t val) {
    MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
    pkt.srcID = myDeviceID; pkt.seqNum = nextSeq();
    pkt.type = PKT_CONFIG; pkt.route = zone;
    pkt.ttl = getSmartTTL(); pkt.configKey = key;
    pkt.configVal = val;
    transmitMesh(&pkt);
}

void sendConfigPacket(uint8_t zone, uint8_t key, const String& val) {
    // NOTE: configStr is 20 bytes (19 usable). For key=3 (parts JSON), this means
    // parts lists must fit in 19 chars per packet. For longer lists, send each
    // part name as a separate PKT_CONFIG packet — the line device appends them.
    // Simple workaround: send individual part names, not a full JSON array.
    if (key == 3 && val.length() > 19) {
        // Parse and send each quoted string as an individual config packet
        // so line device can build the list incrementally.
        // First packet: key=3 with "[" signals start of new parts list
        MeshPacket start; memset(&start, 0, sizeof(start));
        start.srcID = myDeviceID; start.seqNum = nextSeq();
        start.type  = PKT_CONFIG; start.route  = zone;
        start.ttl   = getSmartTTL(); start.configKey = 3;
        strncpy(start.configStr, "[", 19); start.configStr[19] = '\0';
        transmitMesh(&start);
        delay(80);
        // Then send each item
        int i = 0, len = val.length();
        while (i < len) {
            int s = val.indexOf('"', i); if (s < 0) break;
            int e = val.indexOf('"', s + 1); if (e < 0) break;
            String item = val.substring(s + 1, e);
            MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
            pkt.srcID = myDeviceID; pkt.seqNum = nextSeq();
            pkt.type  = PKT_CONFIG; pkt.route  = zone;
            pkt.ttl   = getSmartTTL(); pkt.configKey = 30; // key 30 = append part
            item.toCharArray(pkt.configStr, 19); pkt.configStr[19] = '\0';
            transmitMesh(&pkt);
            delay(80);
            i = e + 1;
        }
        LOG("CFG", "Sent parts to zone %d", zone);
        return;
    }
    MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
    pkt.srcID = myDeviceID; pkt.seqNum = nextSeq();
    pkt.type = PKT_CONFIG; pkt.route = zone;
    pkt.ttl = getSmartTTL(); pkt.configKey = key;
    val.toCharArray(pkt.configStr, sizeof(pkt.configStr));
    pkt.configStr[19] = '\0';
    transmitMesh(&pkt);
}

// ============================================================
// DISPLAY
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

        // Header with better formatting
        display.setTextSize(2); display.setCursor(0, 0);
        display.print("Collector");
        display.setTextSize(1); display.setCursor(155, 2);
        display.print("Peers: "); display.print(numNeighbors);
        display.setCursor(155, 12);
        display.print("Orders: "); display.print(numActiveOrders);
        display.drawLine(0, 20, 295, 20, BLACK);

        if (numActiveOrders == 0) {
            // Empty state with helpful info
            display.setTextSize(2); display.setCursor(0, 38); display.print("Standby");
            display.setTextSize(1); display.setCursor(0, 66);
            display.print("Waiting for orders...");
            if (WiFi.status() == WL_CONNECTED) {
                display.setCursor(0, 80);
                display.print("WiFi: Connected");
            }
        } else {
            // Show active orders with optimized layout
            display.setTextSize(1);
            display.setCursor(0, 23); display.print("ACTIVE ORDERS:");
            display.setCursor(120, 23); display.print("Line");
            display.setCursor(200, 23); display.print("Time");
            display.drawLine(0, 32, 295, 32, BLACK);
            
            int y = 35;
            for (int i = 0; i < numActiveOrders && y <= 108; i++) {
                if (activeOrders[i].claimed || activeOrders[i].timedOut) continue;
                
                // Better visual hierarchy
                if (activeOrders[i].priority == PRIORITY_URGENT) {
                    display.fillRect(0, y-1, 295, 12, BLACK);
                    display.setTextColor(WHITE);
                    display.setCursor(2, y); display.print("!"); display.setCursor(8, y);
                } else {
                    display.setTextColor(BLACK);
                    display.setCursor(2, y);
                }
                
                display.print(String(activeOrders[i].part));
                display.setCursor(120, y);
                display.print(String(activeOrders[i].lineID));
                display.setCursor(200, y);
                display.print(activeOrders[i].timeOrdered);
                y += 13;
            }
        }
        
        // Footer with controls and status
        display.setTextSize(1);
        display.setCursor(0, 120);
        if (WiFi.status() == WL_CONNECTED) {
            display.print("WiFi: OK  ");
        } else {
            display.print("WiFi: ---  ");
        }
        display.print("Boot req");
    }
}


// ============================================================
// WEB PAGES
// ============================================================
const char* dashboardHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MMCall</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;background:#0c0f18;color:#e2e8f0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;overflow-x:hidden}
header{display:flex;align-items:center;gap:10px;padding:10px 14px;background:#080b12;border-bottom:1px solid #1a2540;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:8px;flex-shrink:0}
.logo{font-size:1.1em;font-weight:800;letter-spacing:.12em;color:#f1f5f9;text-transform:uppercase}
.ld{width:8px;height:8px;border-radius:50%;background:#1e2d45;transition:background .4s,box-shadow .4s}
.ld.live{background:#10b981;box-shadow:0 0 7px #10b981aa}
.ld.stale{background:#f59e0b;box-shadow:0 0 7px #f59e0baa}
.pills{display:flex;gap:5px;flex-wrap:wrap;flex:1}
.pill{font-size:.68em;font-weight:700;padding:3px 10px;border-radius:20px;border:1px solid transparent;white-space:nowrap}
.pa{color:#60a5fa;background:#0d1f38;border-color:#1a3a6a}
.pu{color:#fbbf24;background:#1a1100;border-color:#3a2800}
.pc{color:#34d399;background:#031a10;border-color:#064a2a}
.pt{color:#f87171;background:#1a0505;border-color:#4a0f0f}
.pp{color:#94a3b8;background:#111827;border-color:#1e2d45}
#clk{font-size:.82em;font-family:monospace;color:#475569;flex-shrink:0}
#kb{display:flex;gap:8px;padding:10px;overflow-x:auto;min-height:calc(100vh - 84px);align-items:flex-start}
.zcol{flex:0 0 220px;background:#0d1320;border:1px solid #1a2540;border-radius:5px;overflow:hidden}
.zhdr{display:flex;align-items:center;justify-content:space-between;padding:7px 11px;background:#0a101c;border-bottom:1px solid #1a2540}
.zname{font-size:.72em;font-weight:700;letter-spacing:.1em;text-transform:uppercase;color:#64748b}
.zbadge{font-size:.68em;font-weight:700;background:#111827;color:#64748b;border-radius:10px;padding:1px 8px}
.zcards{padding:7px;display:flex;flex-direction:column;gap:5px;min-height:50px}
.card{background:#111827;border:1px solid #1a2540;border-left:3px solid #3b82f6;border-radius:4px;padding:9px 10px;opacity:0;transform:translateY(-6px);transition:opacity .25s,transform .25s,border-left-color .7s}
.card.in{opacity:1;transform:translateY(0)}
.card.out{opacity:0;transform:translateY(6px)}
.chdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:3px}
.cst{font-size:.62em;font-weight:800;letter-spacing:.07em}
.czl{font-size:.62em;color:#334155;font-weight:600}
.cline{font-size:.78em;font-weight:600;color:#94a3b8;margin-bottom:1px}
.cpart{font-size:.85em;font-weight:700;color:#e2e8f0;margin-bottom:5px}
.csep{height:1px;background:#1a2540;margin:4px 0}
.cmeta{display:flex;justify-content:space-between;align-items:center;margin-bottom:4px}
.cmeta span{font-size:.7em;font-family:monospace;color:#475569}
.celap{color:#94a3b8!important;font-weight:700}
.cbar{height:3px;background:#1a2540;border-radius:2px;margin-top:2px;overflow:hidden}
.cbarf{height:3px;border-radius:2px;width:0%;transition:width 1s linear,background .7s}
.card[data-s=claimed] .cst{color:#34d399}
.card[data-s=timeout] .cst{color:#f87171}
.card[data-s=urgent] .cst{color:#fbbf24}
.card[data-s=active] .cst{color:#60a5fa}
.card[data-s=claimed] .celap{color:#34d399!important}
.card[data-s=timeout] .celap{color:#f87171!important}
.zempty{font-size:.72em;color:#1e2d45;text-align:center;padding:18px 0;font-style:italic}
nav{display:flex;border-top:1px solid #1a2540;background:#080b12}
nav a{font-size:.75em;color:#475569;text-decoration:none;padding:8px 13px;transition:color .2s}
nav a:hover{color:#cbd5e1}
</style></head><body>
<header>
<div class="brand"><span class="logo">MMCall</span><span class="ld" id="ld"></span></div>
<div class="pills">
<span class="pill pa" id="pa">0 Active</span>
<span class="pill pu" id="pu">0 Urgent</span>
<span class="pill pc" id="pc">0 Claimed</span>
<span class="pill pt" id="pt">0 Timed Out</span>
<span class="pill pp" id="pp">0 Peers</span>
</div>
<span id="clk">--:--:--</span>
</header>
<div id="kb"><div class="zempty" style="align-self:center;flex:1;font-size:.9em;padding:60px">Waiting for orders&hellip;</div></div>
<nav>
<a href="/settings">&#9881; Settings</a>
<a href="/mesh">&#x1F4E1; Mesh</a>
<a href="/debug">&#x1F4BB; Debug</a>
<a href="/history">&#x1F4CB; History</a>
</nav>
<script>
const TS=2700,cm=new Map(),zm=new Map();let fails=0;
const lerp=(a,b,t)=>{
  const ah=parseInt(a.slice(1),16),bh=parseInt(b.slice(1),16);
  const ar=(ah>>16)&255,ag=(ah>>8)&255,ab=ah&255;
  const br=(bh>>16)&255,bg=(bh>>8)&255,bb=bh&255;
  return'#'+(((Math.round(ar+(br-ar)*t)<<16)|(Math.round(ag+(bg-ag)*t)<<8)|Math.round(ab+(bb-ab)*t)).toString(16).padStart(6,'0'));
};
const pc=p=>p<66.7?lerp('#3b82f6','#f59e0b',p/66.7):lerp('#f59e0b','#ef4444',(p-66.7)/33.3);
const fmt=s=>s<60?s+'s':Math.floor(s/60)+'m '+(s%60<10?'0':'')+(s%60)+'s';
const oid=o=>`${o.zone}|${o.lineID}|${o.part}`;
function mkcard(){
  const d=document.createElement('div');d.className='card';
  d.innerHTML=`<div class="chdr"><span class="cst" data-cst></span><span class="czl" data-czl></span></div><div class="cline" data-cline></div><div class="cpart" data-cpart></div><div class="csep"></div><div class="cmeta"><span data-cord></span><span class="celap" data-cel></span></div><div class="cbar"><div class="cbarf" data-cbar></div></div>`;
  return d;
}
function paint(el,o,sec){
  const pct=Math.min(100,sec/TS*100);
  const col=o.timedOut?'#dc2626':o.claimed?'#34d399':pc(pct);
  el.dataset.s=o.timedOut?'timeout':o.claimed?'claimed':o.priority?'urgent':'active';
  el.querySelector('[data-cst]').textContent=o.timedOut?'TIMED OUT':o.claimed?'CLAIMED':o.priority?'URGENT':'ACTIVE';
  el.querySelector('[data-czl]').textContent='Z'+o.zone;
  el.querySelector('[data-cline]').textContent=o.lineID;
  el.querySelector('[data-cpart]').textContent=o.part;
  el.querySelector('[data-cord]').textContent=o.timeOrdered;
  el.querySelector('[data-cel]').textContent=fmt(sec);
  const bar=el.querySelector('[data-cbar]');
  bar.style.width=pct+'%';bar.style.background=col;
  el.style.borderLeftColor=col;
}
function gzone(z){
  if(zm.has(z))return zm.get(z);
  const c=document.createElement('div');c.className='zcol';c.dataset.z=z;
  c.innerHTML=`<div class="zhdr"><span class="zname">Zone ${z}</span><span class="zbadge" data-cnt>0</span></div><div class="zcards" data-cc></div>`;
  const kb=document.getElementById('kb');
  const emp=kb.querySelector('.zempty[style]');if(emp)kb.removeChild(emp);
  const cs=[...kb.querySelectorAll('.zcol')];
  const af=cs.find(x=>+x.dataset.z>z);
  af?kb.insertBefore(c,af):kb.appendChild(c);
  zm.set(z,c);return c;
}
function recon(data){
  const inc=new Map(data.orders.map(o=>[oid(o),o]));
  cm.forEach((e,id)=>{if(!inc.has(id)&&!e.rm){
    e.rm=true;e.el.classList.add('out');
    setTimeout(()=>{e.el.parentNode&&e.el.parentNode.removeChild(e.el);cm.delete(id);},280);
  }});
  data.orders.forEach(o=>{
    const id=oid(o),now=Date.now();
    if(!cm.has(id)){
      const col=gzone(o.zone),el=mkcard();
      col.querySelector('[data-cc]').appendChild(el);
      cm.set(id,{el,elapsedSec:o.elapsedSec,arrMs:now,frozen:o.frozen,rm:false});
      paint(el,o,o.elapsedSec);
      requestAnimationFrame(()=>el.classList.add('in'));
    }else{
      const e=cm.get(id);if(e.rm)return;
      const sec=e.frozen?e.elapsedSec:e.elapsedSec+Math.floor((now-e.arrMs)/1000);
      e.elapsedSec=o.elapsedSec;e.arrMs=now;e.frozen=o.frozen;
      paint(e.el,o,sec);
    }
  });
  const zc=new Map();
  cm.forEach(e=>{if(e.rm||!e.el.isConnected)return;const zn=e.el.closest('[data-z]');if(zn)zc.set(+zn.dataset.z,(zc.get(+zn.dataset.z)||0)+1);});
  zm.forEach((col,z)=>{
    const n=zc.get(z)||0;col.querySelector('[data-cnt]').textContent=n;
    const cc=col.querySelector('[data-cc]');const em=cc.querySelector('.zempty');
    if(n===0&&!em){const e2=document.createElement('div');e2.className='zempty';e2.textContent='No active orders';cc.appendChild(e2);}
    else if(n>0&&em)cc.removeChild(em);
  });
  let a=0,u=0,c=0,t=0;
  data.orders.forEach(o=>{if(o.timedOut)t++;else if(o.claimed)c++;else{a++;if(o.priority)u++;}});
  document.getElementById('pa').textContent=a+' Active';
  document.getElementById('pu').textContent=u+' Urgent';
  document.getElementById('pc').textContent=c+' Claimed';
  document.getElementById('pt').textContent=t+' Timed Out';
  document.getElementById('pp').textContent=(data.peers||0)+' Peers';
}
function tick(){
  cm.forEach(e=>{
    if(e.frozen||e.rm||!e.el.isConnected)return;
    const sec=e.elapsedSec+Math.floor((Date.now()-e.arrMs)/1000);
    const pct=Math.min(100,sec/TS*100);const col=pc(pct);
    e.el.querySelector('[data-cel]').textContent=fmt(sec);
    const bar=e.el.querySelector('[data-cbar]');
    bar.style.width=pct+'%';bar.style.background=col;
    e.el.style.borderLeftColor=col;
  });
}
function go(){fetch('/api/active').then(r=>r.json()).then(d=>{fails=0;document.getElementById('ld').className='ld live';recon(d);}).catch(()=>{fails++;if(fails>=3)document.getElementById('ld').className='ld stale';});}
function clk(){const n=new Date();document.getElementById('clk').textContent=String(n.getHours()).padStart(2,'0')+':'+String(n.getMinutes()).padStart(2,'0')+':'+String(n.getSeconds()).padStart(2,'0');}
setInterval(go,2000);setInterval(tick,1000);setInterval(clk,1000);go();clk();
</script></body></html>
)rawliteral";

const char* settingsHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><title>Settings – MMCall</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0c0f18;color:#e2e8f0;padding:16px;max-width:760px}
h1{font-size:1.15em;font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:#f1f5f9;margin-bottom:14px}
h2{font-size:.8em;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:#64748b;margin-bottom:10px}
.card{background:#111827;border:1px solid #1a2540;border-radius:5px;padding:14px;margin-bottom:10px}
label{display:block;font-size:.8em;font-weight:600;color:#64748b;margin:8px 0 3px}
input,textarea{background:#0c0f18;color:#e2e8f0;border:1px solid #1a2540;border-radius:4px;padding:6px 9px;font-size:.82em}
textarea{width:100%;resize:vertical}
input[type=submit]{background:#0d1f38;color:#60a5fa;border:1px solid #1a3a6a;padding:7px 16px;border-radius:4px;cursor:pointer;font-size:.8em;font-weight:700;margin-top:8px}
input[type=submit]:hover{background:#132d52}
table{border-collapse:collapse;width:100%;font-size:.8em}
td,th{border:1px solid #1a2540;padding:6px 9px;text-align:left}
th{background:#0a101c;color:#64748b;font-weight:700}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:6px}
a{color:#60a5fa;text-decoration:none;font-size:.82em}
.sub{font-size:.75em;color:#334155;margin:4px 0 8px}
</style></head><body>
<h1>&#9881; Settings</h1>
<div class="card">
<h2>&#x1F4E1; Detected Devices</h2>
%DEVICE_TABLE%
<p class="sub">Devices appear automatically from heartbeats.</p>
</div>
<div class="card">
<h2>&#x1F4E6; Configure Parts for Zone</h2>
<form action="/api/config" method="post">
<label>Target Zone(s) (comma-separated)</label>
<input name="zones" placeholder="1,2,3" size="16">
<label>Parts JSON</label>
<textarea name="parts" rows="3">["Pod Pickup","Empty Cart","Maintenance","Supervisor"]</textarea>
<input type="submit" value="Send Parts Config">
</form>
</div>
<div class="card">
<h2>&#x1F50D; Mesh Tools</h2>
<div class="row">
<form action="/api/catchup" method="post"><input type="submit" value="Rebroadcast Orders"></form>
<form action="/api/timesync" method="post"><input type="submit" value="Broadcast Time Sync"></form>
<form action="/api/clear_claimed" method="post"><input type="submit" value="Archive &amp; Clear Claimed"></form>
</div>
</div>
<div class="card">
<h2>&#x1F310; WiFi Client (NTP)</h2>
<p class="sub">Connect to warehouse WiFi to sync real time via NTP, then broadcast to all devices.</p>
<form action="/api/setwifi" method="post">
<label>SSID</label><input name="ssid" size="26" placeholder="WarehouseWiFi">
<label>Password</label><input name="pass" type="password" size="26">
<input type="submit" value="Save &amp; Reconnect Next Boot">
</form>
</div>
<div class="card">
<h2>&#x1F4E1; Push WiFi OTA to Mesh</h2>
<p class="sub">Sends credentials over LoRa so Tuggers and Line Devices can receive OTA updates. Max 19 chars each.</p>
<form action="/api/pushwifi" method="post">
<label>SSID (max 19 chars)</label><input name="ssid" size="20" placeholder="WarehouseWiFi">
<label>Password (max 19 chars)</label><input name="pass" type="password" size="20">
<input type="submit" value="Push Credentials to All Devices">
</form>
</div>
<p style="margin-top:12px"><a href="/">&#8592; Dashboard</a> &nbsp;|&nbsp; <a href="/debug">&#x1F4BB; Debug</a></p>
</body></html>
)rawliteral";

const char* meshHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><title>Mesh – MMCall</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0c0f18;color:#e2e8f0;padding:16px}
h1{font-size:1.15em;font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:#f1f5f9;margin-bottom:14px}
.grid{display:flex;flex-wrap:wrap;gap:8px}
.peer{background:#111827;border:1px solid #1a2540;border-left:3px solid;border-radius:5px;padding:11px 14px;min-width:180px}
.peer.line{border-left-color:#34d399}
.peer.tug{border-left-color:#60a5fa}
.ptype{font-size:.62em;font-weight:800;letter-spacing:.08em;text-transform:uppercase;margin-bottom:3px}
.peer.line .ptype{color:#34d399}
.peer.tug .ptype{color:#60a5fa}
.plabel{font-size:.88em;font-weight:700;color:#e2e8f0;margin-bottom:6px}
.pmeta{font-size:.72em;color:#475569;font-family:monospace}
.pmeta div{margin-bottom:2px}
#empty{font-size:.85em;color:#334155;padding:30px 0;font-style:italic}
a{color:#60a5fa;text-decoration:none;font-size:.82em}
</style></head><body>
<h1>&#x1F4E1; Mesh Network</h1>
<div class="grid" id="g"><div id="empty">Loading&hellip;</div></div>
<p style="margin-top:14px"><a href="/">&#8592; Dashboard</a></p>
<script>
fetch('/api/mesh').then(r=>r.json()).then(d=>{
  const g=document.getElementById('g'),emp=document.getElementById('empty');
  if(d.peers&&d.peers.length){
    if(emp)g.removeChild(emp);
    d.peers.forEach(p=>{
      const div=document.createElement('div');div.className='peer '+(p.isLine?'line':'tug');
      div.innerHTML=`<div class="ptype">${p.isLine?'Line Device':'Tugger'}</div><div class="plabel">${p.label}</div><div class="pmeta"><div>Zone ${p.zone}</div><div>${p.rssi} dBm &nbsp; ${p.hops} hops</div><div>${p.ago}s ago</div></div>`;
      g.appendChild(div);});
  }else if(emp)emp.textContent='No peers detected';
});
</script></body></html>
)rawliteral";

const char* historyHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><title>History – MMCall</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0c0f18;color:#e2e8f0;padding:16px}
h1{font-size:1.15em;font-weight:800;letter-spacing:.08em;text-transform:uppercase;color:#f1f5f9;margin-bottom:14px}
table{border-collapse:collapse;width:100%;background:#111827;border:1px solid #1a2540;border-radius:5px;overflow:hidden}
th{background:#0a101c;color:#64748b;font-size:.72em;font-weight:700;letter-spacing:.06em;text-transform:uppercase;padding:8px 10px;text-align:left}
td{border-top:1px solid #1a2540;padding:7px 10px;font-size:.82em}
.ok td:last-child{color:#34d399;font-weight:700}
.late td:last-child{color:#f87171;font-weight:700}
input[type=submit]{background:#1a0505;color:#f87171;border:1px solid #4a0f0f;padding:7px 14px;border-radius:4px;cursor:pointer;font-size:.8em;font-weight:700;margin-top:12px}
input[type=submit]:hover{background:#2d0a0a}
a{color:#60a5fa;text-decoration:none;font-size:.82em}
.empty{text-align:center;color:#1e2d45;padding:30px;font-style:italic;font-size:.85em}
</style></head><body>
<h1>&#x1F4CB; Shift History</h1>
<table>
<thead><tr><th>Line</th><th>Part</th><th>Zone</th><th>Ordered</th><th>Response</th><th>Result</th></tr></thead>
<tbody id="tb"></tbody>
</table>
<form action="/api/clear_history" method="post"><input type="submit" value="Clear History"></form>
<p style="margin-top:12px"><a href="/">&#8592; Dashboard</a></p>
<script>
fetch('/api/history').then(r=>r.json()).then(d=>{
  const tb=document.getElementById('tb');
  if(!d.history||!d.history.length){tb.innerHTML='<tr><td colspan="6" class="empty">No history this shift</td></tr>';return;}
  tb.innerHTML=d.history.map(h=>`<tr class="${h.claimed?'ok':'late'}"><td>${h.lineID}</td><td>${h.part}</td><td>Z${h.zone}</td><td>${h.timeOrdered}</td><td>${h.elapsed}</td><td>${h.claimed?'&#10003; Claimed':'&#9888; Timed Out'}</td></tr>`).join('');
});
</script></body></html>
)rawliteral";

// ============================================================
// WEB SERVER
// ============================================================
void setupWebServer() {
    WiFi.softAP(AP_SSID, AP_PASS);

    server.on("/", []() { server.send(200, "text/html", dashboardHTML); });
    server.on("/mesh",    []() { server.send(200, "text/html", meshHTML); });
    server.on("/history", []() { server.send(200, "text/html", historyHTML); });

    server.on("/api/active", []() {
        JsonDocument doc;
        JsonArray arr = doc["orders"].to<JsonArray>();
        int peers = 0;
        for (int i = 0; i < MAX_NEIGHBORS; i++)
            if (neighbors[i].lastSeen && millis() - neighbors[i].lastSeen < 120000UL) peers++;
        doc["peers"] = peers;
        for (int i = 0; i < numActiveOrders; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["lineID"]      = activeOrders[i].lineID;
            o["part"]        = activeOrders[i].part;
            o["zone"]        = activeOrders[i].zone;
            o["priority"]    = activeOrders[i].priority;
            o["timeOrdered"] = activeOrders[i].timeOrdered;
            o["claimed"]  = activeOrders[i].claimed;
            o["timedOut"] = activeOrders[i].timedOut;
            o["frozen"]   = (activeOrders[i].claimed || activeOrders[i].timedOut);
            unsigned long elapsed;
            if (activeOrders[i].claimed && activeOrders[i].claimedAt > 0)
                elapsed = (activeOrders[i].claimedAt - activeOrders[i].timestamp) / 1000;
            else if (activeOrders[i].timedOut)
                elapsed = 45UL * 60;
            else
                elapsed = (millis() - activeOrders[i].timestamp) / 1000;
            o["elapsedSec"] = elapsed;
        }
        String js; serializeJson(doc, js);
        server.send(200, "application/json", js);
    });

    server.on("/api/mesh", []() {
        JsonDocument doc; JsonArray arr = doc["peers"].to<JsonArray>();
        for (int i = 0; i < MAX_NEIGHBORS; i++) {
            if (!neighbors[i].lastSeen) continue;
            JsonObject o = arr.add<JsonObject>();
            o["label"]  = neighbors[i].label;
            o["zone"]   = neighbors[i].zone;
            o["rssi"]   = neighbors[i].rssi;
            o["hops"]   = neighbors[i].lastHopCount;
            o["ago"]    = (millis() - neighbors[i].lastSeen) / 1000;
            o["isLine"] = neighbors[i].isLineDevice;
        }
        String js; serializeJson(doc, js);
        server.send(200, "application/json", js);
    });

    server.on("/api/history", []() {
        JsonDocument doc; JsonArray arr = doc["history"].to<JsonArray>();
        for (int i = 0; i < numHistory; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["lineID"]      = history[i].lineID;
            o["part"]        = history[i].part;
            o["zone"]        = history[i].zone;
            o["timeOrdered"] = history[i].timeOrdered;
            o["elapsed"] = String(history[i].elapsedSec/60)+"m "+String(history[i].elapsedSec%60)+"s";
            o["claimed"] = history[i].claimed;
        }
        String js; serializeJson(doc, js);
        server.send(200, "application/json", js);
    });

    server.on("/settings", []() {
        String page = String(settingsHTML);
        // Build auto-detected device table from neighbor table
        String tbl = "<table><tr><th>Type</th><th>Label</th><th>Zone</th><th>RSSI</th><th>Last Seen</th><th>Push Parts</th></tr>";
        bool any = false;
        for (int i = 0; i < MAX_NEIGHBORS; i++) {
            if (!neighbors[i].lastSeen || millis() - neighbors[i].lastSeen > 300000UL) continue;
            unsigned long ago = (millis() - neighbors[i].lastSeen) / 1000;
            String type = neighbors[i].isLineDevice ? "Line" : "Tugger";
            tbl += "<tr><td>" + type + "</td><td>" + String(neighbors[i].label) +
                   "</td><td>Z" + String(neighbors[i].zone) +
                   "</td><td>" + String(neighbors[i].rssi) + " dBm</td>" +
                   "<td>" + String(ago) + "s ago</td>" +
                   "<td><form action='/api/config' method='post'>" +
                   "<input type='hidden' name='zones' value='" + String(neighbors[i].zone) + "'>" +
                   "<input type='hidden' name='parts' value='[\"Pod Pickup\",\"Empty Cart\"]'>" +
                   "<input type='submit' value='Push'></form></td></tr>";
            any = true;
        }
        if (!any) tbl += "<tr><td colspan='6' style='color:#888'>No devices detected yet — waiting for heartbeats</td></tr>";
        tbl += "</table>";
        page.replace("%DEVICE_TABLE%", tbl);
        server.send(200, "text/html", page);
    });

    server.on("/api/config", []() {
        if (server.method() == HTTP_POST && server.hasArg("zones") && server.hasArg("parts")) {
            String zonesStr = server.arg("zones");
            String partsJson = server.arg("parts");
            int start = 0;
            while (true) {
                int comma = zonesStr.indexOf(',', start);
                String zStr = (comma > 0) ? zonesStr.substring(start, comma) : zonesStr.substring(start);
                if (zStr.length() > 0) sendConfigPacket(zStr.toInt(), 3, partsJson);
                if (comma < 0) break; start = comma + 1;
            }
        }
        server.sendHeader("Location", "/settings"); server.send(303);
    });

    server.on("/api/catchup", []() {
        broadcastCatchup();
        server.sendHeader("Location", "/settings"); server.send(303);
    });

    server.on("/api/timesync", []() {
        broadcastTimeSync();
        server.sendHeader("Location", "/settings"); server.send(303);
    });

    server.on("/api/clear_claimed", []() {
        // Archive to history
        for (int i = 0; i < numActiveOrders; i++) {
            if (!activeOrders[i].claimed && !activeOrders[i].timedOut) continue;
            if (numHistory < MAX_HISTORY) {
                int h = numHistory; // Increment AFTER slot written
                history[h] = {}; // Zero-init before strncpy to clear stale bytes
                strncpy(history[h].lineID, activeOrders[i].lineID, 23);
                history[h].lineID[23] = '\0';
                strncpy(history[h].part,   activeOrders[i].part,   23);
                history[h].part[23] = '\0';
                strncpy(history[h].timeOrdered, activeOrders[i].timeOrdered, 5);
                history[h].timeOrdered[5] = '\0';
                history[h].elapsedSec = (millis() - activeOrders[i].timestamp) / 1000;
                history[h].zone    = activeOrders[i].zone;
                history[h].claimed = activeOrders[i].claimed;
                numHistory++; // Increment after slot is fully written
            }
        }
        // Compact
        int newCount = 0;
        for (int i = 0; i < numActiveOrders; i++) {
            if (!activeOrders[i].claimed && !activeOrders[i].timedOut) {
                if (i != newCount) activeOrders[newCount] = activeOrders[i];
                newCount++;
            }
        }
        numActiveOrders = newCount;
        saveOrdersToNVS(); smartUpdateDisplay();
        server.sendHeader("Location", "/"); server.send(303);
    });

    server.on("/api/clear_history", []() {
        numHistory = 0;
        server.sendHeader("Location", "/history"); server.send(303);
    });


    // WiFi STA credentials — set from browser so NTP works
    // Accessible at /settings, stored in NVS key "wifi_ssid" / "wifi_pass"
    server.on("/api/setwifi", []() {
        if (server.method() == HTTP_POST &&
            server.hasArg("ssid") && server.hasArg("pass")) {
            String ssid = server.arg("ssid");
            String pass = server.arg("pass");
            if (ssid.length() > 0 && ssid.length() <= 63 && pass.length() <= 63) {
                prefs.begin("collector", false);
                prefs.putString("wifi_ssid", ssid);
                prefs.putString("wifi_pass", pass);
                prefs.end();
                LOG("WIFI", "Credentials saved: %s", ssid.c_str());
            }
        }
        server.sendHeader("Location", "/settings"); server.send(303);
    });

    // Push WiFi credentials to all mesh devices via PKT_CONFIG
    server.on("/api/pushwifi", [&]() {
        if (server.method() == HTTP_POST &&
            server.hasArg("ssid") && server.hasArg("pass")) {
            String ssid = server.arg("ssid");
            String pass = server.arg("pass");
            if (ssid.length() > 0 && ssid.length() <= 19 && pass.length() <= 19) {
                // key 11 = wifi_ssid, key 12 = wifi_pass, key 9 = enable+reconnect
                // Send to route 0 (all-call) — TTL carries to all zones
                sendConfigPacket(0, 11, ssid); delay(200);
                sendConfigPacket(0, 12, pass); delay(200);
                // Enable packet uses configVal=1, send as raw packet
                MeshPacket en; memset(&en, 0, sizeof(en));
                en.srcID = myDeviceID; en.seqNum = nextSeq();
                en.type = PKT_CONFIG; en.route = 0;
                en.ttl = getSmartTTL(); en.configKey = 9; en.configVal = 1;
                isDuplicate(myDeviceID, en.seqNum);
                transmitMesh(&en);
                LOG("CFG", "WiFi creds pushed to mesh: %s", ssid.c_str());
            }
        }
        server.sendHeader("Location", "/settings"); server.send(303);
    });

    // /api/log — JSON log ring buffer
    server.on("/api/log", []() {
        String js = "{\"entries\":[";
        int count = (logCount < LOG_BUF_SIZE) ? logCount : LOG_BUF_SIZE;
        int start = (logHead - count + LOG_BUF_SIZE * 2) % LOG_BUF_SIZE;
        for (int i = 0; i < count; i++) {
            int idx = (start + i) % LOG_BUF_SIZE;
            if (i > 0) js += ",";
            js += "{\"ms\":"; js += logBuf[idx].ms;
            js += ",\"msg\":\"";
            for (char* p = logBuf[idx].msg; *p; p++) {
                if (*p == '"') js += "\\\"";
                else if (*p == '\\') js += "\\\\";
                else js += *p;
            }
            js += "\"}";
        }
        js += "],\"total\":"; js += logHead; js += "}";
        server.send(200, "application/json", js);
    });

    // /api/inject — inject test commands
    server.on("/api/inject", []() {
        if (server.method() != HTTP_POST) { server.send(405); return; }
        JsonDocument doc;
        if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
            server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
        }
        String cmd = doc["cmd"] | "";
        if (cmd == "call") {
            uint8_t zone = doc["zone"] | 1;
            String item  = doc["item"] | "TestLine|TestPart";
            if (numActiveOrders < MAX_ACTIVE_ORDERS) {
                int idx = numActiveOrders;
                activeOrders[idx] = {};
                int sep = item.indexOf('|');
                String ln = (sep>0) ? item.substring(0,sep) : item;
                String pt = (sep>0) ? item.substring(sep+1) : "";
                ln.toCharArray(activeOrders[idx].lineID, 24);
                pt.toCharArray(activeOrders[idx].part, 24);
                String t = epochToHHMM(getCurrentEpoch());
                t.toCharArray(activeOrders[idx].timeOrdered, 6);
                activeOrders[idx].srcID = myDeviceID;
                activeOrders[idx].seqNum = nextSeq();
                activeOrders[idx].timestamp = millis();
                activeOrders[idx].zone = zone;
                activeOrders[idx].priority = doc["priority"] | 0;
                numActiveOrders++;
                saveOrdersToNVS(); smartUpdateDisplay();
                LOG("INJECT", "call %s Z%d", item.c_str(), zone);
            }
            server.send(200, "application/json", "{\"ok\":true}");
        } else if (cmd == "claim") {
            String item = doc["item"] | "";
            for (int i = 0; i < numActiveOrders; i++) {
                String full = String(activeOrders[i].lineID)+"|"+String(activeOrders[i].part);
                if (!activeOrders[i].claimed && (item.length()==0 || full==item || String(activeOrders[i].lineID)==item)) {
                    activeOrders[i].claimed = true; activeOrders[i].claimedAt = millis();
                }
            }
            saveOrdersToNVS(); smartUpdateDisplay();
            server.send(200, "application/json", "{\"ok\":true}");
        } else if (cmd == "catchup") {
            broadcastCatchup();
            server.send(200, "application/json", "{\"ok\":true}");
        } else if (cmd == "timesync") {
            broadcastTimeSync();
            server.send(200, "application/json", "{\"ok\":true}");
        } else if (cmd == "wipe") {
            prefs.begin("collector", false); prefs.clear(); prefs.end();
            server.send(200, "application/json", "{\"ok\":true,\"msg\":\"rebooting\"}");
            delay(200); ESP.restart();
        } else if (cmd == "status") {
            String js = "{\"uptime\":"; js += millis()/1000;
            js += ",\"orders\":"; js += numActiveOrders;
            js += ",\"peers\":"; js += numNeighbors;
            js += ",\"ntpSynced\":"; js += ntpSynced ? "true" : "false";
            js += ",\"txQ\":"; js += txQCount;
            js += ",\"wifiOta\":"; js += (WiFi.status()==WL_CONNECTED) ? "true":"false";
            js += "}";
            server.send(200, "application/json", js);
        } else {
            server.send(400, "application/json", "{\"error\":\"unknown cmd\"}");
        }
    });

    // /debug — live web debug console
    server.on("/debug", []() {
        server.send(200, "text/html", R"rawliteral(<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><title>Debug – MMCall</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0c0f18;color:#e2e8f0;padding:10px}
h2{font-size:1em;font-weight:800;letter-spacing:.1em;text-transform:uppercase;color:#f1f5f9;margin-bottom:8px}
#log{height:58vh;overflow-y:auto;background:#060913;padding:8px;border:1px solid #1a2540;border-radius:4px;font-size:12px;font-family:monospace;white-space:pre-wrap;margin-bottom:8px}
.tCALL{color:#f59e0b}.tCLAIM{color:#34d399}.tNTP{color:#60a5fa}
.tBOOT{color:#a78bfa}.tTIMEOUT{color:#f87171}.tREMOTE{color:#f0abfc}
.tOTA{color:#38bdf8}.tCFG{color:#4ade80}.tINJECT{color:#fbbf24}.tdef{color:#64748b}
.row{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:6px;align-items:center}
.btn{background:#0d1f38;color:#60a5fa;border:1px solid #1a3a6a;padding:5px 11px;cursor:pointer;border-radius:4px;font-size:.78em;font-weight:700;font-family:inherit}
.btn:hover{background:#132d52}
.btn.warn{background:#1a0505;color:#f87171;border-color:#4a0f0f}
.btn.warn:hover{background:#2d0a0a}
input{background:#0c0f18;color:#e2e8f0;border:1px solid #1a2540;border-radius:4px;padding:5px 8px;font-size:.78em;font-family:monospace}
#st{font-size:.72em;color:#334155;margin-left:4px}
a{color:#60a5fa;font-size:.78em;text-decoration:none}
</style></head><body>
<h2>&#x1F4E1; Debug Console</h2>
<div class="row">
  <button class="btn" onclick="inj({cmd:'catchup'})">Force Catchup</button>
  <button class="btn" onclick="inj({cmd:'timesync'})">Force TimeSync</button>
  <button class="btn" onclick="inj({cmd:'status'}).then(r=>r.json()).then(d=>alert(JSON.stringify(d,null,2)))">Status</button>
  <button class="btn warn" onclick="if(confirm('Wipe NVS and reboot?'))inj({cmd:'wipe'})">Wipe + Reboot</button>
</div>
<div class="row">
  <input id="zn" value="1" style="width:44px" placeholder="Z">
  <input id="it" value="TestLine|Pod Pickup" style="width:180px">
  <button class="btn" onclick="inj({cmd:'call',zone:+g('zn').value,item:g('it').value})">Inject Call</button>
  <button class="btn" onclick="inj({cmd:'claim',item:g('it').value})">Inject Claim</button>
  <button class="btn" onclick="clearLog()">Clear</button>
  <span id="st"></span>
</div>
<div id="log"></div>
<div style="margin-top:6px"><a href="/">&#8592; Dashboard</a></div>
<script>
var last=0;
function g(x){return document.getElementById(x)}
function cl(msg){var m=msg.match(/^\[(\w+)/);var t=m?m[1]:'def';return '<span class="t'+t+'">'+msg.replace(/</g,'&lt;')+'</span>';}
function pad(n){return n<10?'0'+n:n}
function ts(ms){var s=Math.floor(ms/1000);return pad(Math.floor(s/3600)%24)+':'+pad(Math.floor(s/60)%60)+':'+pad(s%60);}
function poll(){fetch('/api/log').then(r=>r.json()).then(d=>{
  if(d.total!==last){
    var div=g('log'),ab=div.scrollTop+div.clientHeight>=div.scrollHeight-20;
    var h='';d.entries.forEach(e=>{h+=ts(e.ms)+' '+cl(e.msg)+'\n';});
    div.innerHTML=h;if(ab)div.scrollTop=div.scrollHeight;
    last=d.total;g('st').textContent=new Date().toLocaleTimeString()+' ('+d.total+')';
  }
}).catch(()=>{g('st').textContent='offline';});}
function inj(c){return fetch('/api/inject',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(c)});}
function clearLog(){g('log').innerHTML='';last=0;}
setInterval(poll,1500);poll();
</script></body></html>)rawliteral");
    });

    server.begin();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);

    prefs.begin("collector", false);
    myDeviceID = prefs.getUInt("device_id", 0);
    mySeqNum   = prefs.getUInt("my_seq",    0);
    prefs.end();

    if (myDeviceID == 0) {
        myDeviceID = esp_random();
        prefs.begin("collector", false); prefs.putUInt("device_id", myDeviceID); prefs.end();
    }
    mySeqNum += 1000;
    prefs.begin("collector", false); prefs.putUInt("my_seq", mySeqNum); prefs.end();

    randomSeed(esp_random());
    memset(activeOrders,  0, sizeof(activeOrders));
    memset(seqTable,      0, sizeof(seqTable));
    memset(neighbors,     0, sizeof(neighbors));
    memset(history,       0, sizeof(history));
    memset(txQueue,       0, sizeof(txQueue));

    loadOrdersFromNVS();

    display.begin(); smartUpdateDisplay();

    SPI.begin(RADIO_SCLK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);
    int state = radio.begin(RF_FREQUENCY, BANDWIDTH, SPREADING_FACTOR,
                            CODING_RATE, SYNC_WORD, OUTPUT_POWER, PREAMBLE_LENGTH);
    if (state != RADIOLIB_ERR_NONE) { LOG("RADIO", "Failed: %d", state); while(true); }
    radio.setDio1Action(onReceive);
    radio.startReceive();

    setupWebServer();

    // Try NTP — WiFi AP is up, try to also connect as client if credentials stored
    prefs.begin("collector", true);
    String ssid = prefs.getString("wifi_ssid", "");
    String pass = prefs.getString("wifi_pass", "");
    prefs.end();
    if (ssid.length() > 0) {
        WiFi.begin(ssid.c_str(), pass.c_str());
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) delay(100);
        if (WiFi.status() == WL_CONNECTED) {
            timeClient.begin(); timeClient.update();
            ntpSynced = true;
            LOG("NTP", "Synced: %s", timeClient.getFormattedTime().c_str());
            broadcastTimeSync();
            ArduinoOTA.setHostname("MMCall-Collector");
            ArduinoOTA.begin();
            LOG("OTA", "WiFi OTA ready on MMCall-Collector");
        }
    }

    delay(300);
    broadcastCatchup();

    LOG("BOOT", "Collector ready ID=0x%08X", myDeviceID);
    smartUpdateDisplay();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    server.handleClient();
    if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

    // NTP update every 10 minutes
    static unsigned long lastNTP = 0;
    if (ntpSynced && millis() - lastNTP > 600000UL) {
        lastNTP = millis();
        timeClient.update();
        broadcastTimeSync();
    }

    // Catchup every 30 seconds
    static unsigned long lastCatchup = 0;
    if (millis() - lastCatchup > 30000UL) {
        lastCatchup = millis();
        broadcastCatchup();
    }

    // TX queue drain — direct transmit with CAD only, no blocking backoff
    // transmitMesh() would add 20-300ms of delay() which freezes handleClient()
    if (txQCount > 0 && millis() - lastTX > 150) {
        if (radio.scanChannel() == RADIOLIB_CHANNEL_FREE) {
            radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]),
                           sizeof(MeshPacket));
            lastTX = millis();
            txQHead = (txQHead + 1) % TX_QUEUE_SIZE;
            txQCount--;
        }
        radio.startReceive();
    }

    if (rxFlag) {
        rxFlag = false;
        MeshPacket pkt; memset(&pkt, 0, sizeof(pkt));
        int state = radio.readData(reinterpret_cast<uint8_t*>(&pkt), sizeof(MeshPacket));
        int8_t pktRSSI = (int8_t)radio.getRSSI();

        if (state == RADIOLIB_ERR_NONE) {
            // Determine if this is a line device from heartbeat item format "LineID|ZN"
            bool isLine = false;
            uint8_t peerZone = pkt.route;
            char peerLabel[28] = {};
            strncpy(peerLabel, pkt.item, 27);
            peerLabel[27] = '\0'; // Explicit null — strncpy(27) doesn't guarantee byte[27]

            if (pkt.type == PKT_HEARTBEAT) {
                String itm = String(pkt.item);
                // Line device heartbeats: "Line 112|Z1"
                // Tugger heartbeats: "Tugger|Z1"
                isLine = (itm.indexOf("Tugger") < 0 && itm.indexOf('|') > 0);
                int pipe = itm.indexOf('|');
                if (pipe > 0) {
                    String zPart = itm.substring(pipe + 1);
                    if (zPart.startsWith("Z") && zPart.length() > 1)
                        peerZone = zPart.substring(1).toInt();
                }
            }

            updateNeighbor(pkt.srcID, pktRSSI, pkt.hopCount, peerLabel, peerZone, isLine);

            if (pkt.srcID != myDeviceID && !isDuplicate(pkt.srcID, pkt.seqNum)) {
                // Forward
                if (pkt.type != PKT_CONFIG && pkt.type != PKT_CATCHUP &&
                    pkt.type != PKT_TIMESYNC && pkt.ttl > 0) {
                    pkt.ttl--; pkt.hopCount++; txEnqueue(&pkt);
                }

                if (pkt.type == PKT_CALL) {
                    String itemStr = String(pkt.item);
                    int sep = itemStr.indexOf('|');
                    String lineStr = (sep > 0) ? itemStr.substring(0, sep) : itemStr;
                    String partStr = (sep > 0) ? itemStr.substring(sep + 1) : "";
                    // BUG FIX: If same line+part already active, update seqNum
                    // but don't create a second entry. A second press on the line
                    // device sends a new seqNum which previously bypassed dedup.
                    int existingIdx = -1;
                    for (int i = 0; i < numActiveOrders; i++) {
                        if (!activeOrders[i].claimed &&
                            strcmp(activeOrders[i].lineID, lineStr.c_str()) == 0 &&
                            strcmp(activeOrders[i].part,   partStr.c_str()) == 0) {
                            existingIdx = i; break;
                        }
                    }
                    if (existingIdx >= 0) {
                        // Already have this order — just update seqNum reference
                        activeOrders[existingIdx].seqNum = pkt.seqNum;
                        LOG("CALL", "Dedup update for %s|%s", lineStr.c_str(), partStr.c_str());
                    }
                    if (existingIdx < 0 && numActiveOrders < MAX_ACTIVE_ORDERS) {
                        int idx = numActiveOrders; // Increment AFTER slot is fully written
                        activeOrders[idx] = {};    // Zero-init the slot first
                        activeOrders[idx].srcID    = pkt.srcID;
                        activeOrders[idx].seqNum   = pkt.seqNum;
                        lineStr.toCharArray(activeOrders[idx].lineID, 24);
                        partStr.toCharArray(activeOrders[idx].part,   24);
                        String t = epochToHHMM(getCurrentEpoch());
                        t.toCharArray(activeOrders[idx].timeOrdered, 6);
                        activeOrders[idx].timestamp = millis();
                        activeOrders[idx].zone      = pkt.route;
                        activeOrders[idx].priority  = pkt.priority;
                        activeOrders[idx].claimed   = false;
                        activeOrders[idx].timedOut  = false;
                        numActiveOrders++; // Only increment after full write
                        saveOrdersToNVS(); smartUpdateDisplay();
                        LOG("CALL", "%s|%s Z%d t=%s", lineStr.c_str(), partStr.c_str(),
                            pkt.route, activeOrders[idx].timeOrdered);
                    }
                }

                if (pkt.type == PKT_CLAIM) {
                    String claimed = String(pkt.item);
                    claimed.trim(); // Symmetric with line/part trim below
                    bool changed = false;
                    for (int i = 0; i < numActiveOrders; i++) {
                        if (activeOrders[i].claimed) continue;
                        String full = String(activeOrders[i].lineID) + "|" + String(activeOrders[i].part);
                        String lineOnly = String(activeOrders[i].lineID);
                        full.trim(); lineOnly.trim();
                        if (claimed == full || claimed == lineOnly) {
                            activeOrders[i].claimed   = true;
                            activeOrders[i].claimedAt = millis();
                            changed = true;
                        }
                    }
                    if (changed) { saveOrdersToNVS(); smartUpdateDisplay(); }
                }

                if (pkt.type == PKT_BOOT_REQ) {
                    LOG("BOOT_REQ", "From 0x%08X — sending catchup+timesync", pkt.srcID);
                    delay(150);
                    broadcastTimeSync();
                    delay(50);
                    broadcastCatchup();
                }

                if (pkt.type == PKT_DEBUG) {
                    char remote[92];
                    snprintf(remote, sizeof(remote), "[REMOTE:0x%08X] %s", pkt.srcID, pkt.item);
                    logWrite(remote);
                }
            }
        }
        radio.startReceive();
    }

    // 45-minute timeout + management alert
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 10000UL) {
        lastCheck = millis();
        bool changed = false;
        for (int i = 0; i < numActiveOrders; i++) {
            if (activeOrders[i].claimed || activeOrders[i].timedOut) continue;
            unsigned long elapsed = millis() - activeOrders[i].timestamp;
            if (elapsed > 45UL * 60 * 1000) {
                activeOrders[i].timedOut = true;
                changed = true;
                LOG("TIMEOUT", "%s|%s Z%d ordered=%s",
                    activeOrders[i].lineID, activeOrders[i].part,
                    activeOrders[i].zone, activeOrders[i].timeOrdered);
            }
        }
        if (changed) { saveOrdersToNVS(); smartUpdateDisplay(); }
    }
}
