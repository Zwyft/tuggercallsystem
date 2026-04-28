// ============================================================
// COLLECTOR FIRMWARE — Heltec Vision Master E290
// MMCall Mesh Network — v1.1
// ============================================================

#include <Arduino.h>
#include <LittleFS.h>
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

// Heltec Vision Master E290 e-ink display optimizations
void initDisplay() {
    // Optimize for Heltec Vision Master E290 specific characteristics
    display.setRotation(3);  // Portrait mode
    display.setFactor(0.75f);  // Adjust for better text clarity
    display.setLut(gDefaultLut);  // Use optimized lookup table
    display.setPonWaitTime(100);  // Power on wait time optimized for this panel
    display.setPoffWaitTime(100); // Power off wait time
}

void smartUpdateDisplay(bool forceFull = false) {
    updateDisplay(); // draws content then applies content-aware refresh
}

DEPG0290BNS800 display;
SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);
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
// FORWARD DECLARATIONS
// ============================================================
void transmitMesh(void* p);
void broadcastCatchup();
void smartUpdateDisplay();

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
        Serial.printf("[NVS] Version mismatch (%d vs %d) — wiping orders\n",
            storedVer, FW_VERSION);
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
    Serial.printf("[NVS] Loaded %d orders\n", numActiveOrders);
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
    Serial.printf("[NTP] Broadcast epoch=%lu\n", (unsigned long)epoch);
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
    Serial.printf("[CATCHUP] Sent %d orders\n", sent);
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
        Serial.printf("[CFG] Sent %d parts to zone %d\n", 0, zone);
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
    display.fastmodeOn();
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
    display.fastmodeOff();
    // Heltec Vision Master E290 optimization: intelligent refresh strategy
    static unsigned long lastFullRefresh = 0;
    static unsigned long lastPartialRefresh = 0;
    
    // Full refresh every 30 seconds or after major state changes
    if (millis() - lastFullRefresh > 30000) {
        display.fullRefresh();
        lastFullRefresh = millis();
        lastPartialRefresh = millis();
    } 
    // Partial refresh for minor updates (faster, less flicker)
    else if (millis() - lastPartialRefresh > 5000) {
        display.partialRefresh();
        lastPartialRefresh = millis();
    }
}


// ============================================================
// WEB PAGES
// ============================================================
const char* dashboardHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>MMCall Collector</title>
<style>
*{box-sizing:border-box}
body{font-family:sans-serif;background:#f0f0f0;margin:0;padding:16px}
h1{margin:0 0 6px;font-size:1.4em}
.stats{display:flex;gap:10px;margin-bottom:12px;flex-wrap:wrap}
.stat{background:#fff;border-radius:6px;padding:8px 14px;box-shadow:0 1px 3px rgba(0,0,0,.15);min-width:80px;text-align:center}
.stat span{font-size:1.8em;font-weight:bold;display:block}
table{border-collapse:collapse;width:100%;background:#fff;border-radius:6px;overflow:hidden;box-shadow:0 1px 3px rgba(0,0,0,.12)}
th{background:#37474f;color:#fff;padding:9px 8px;text-align:left;font-size:.85em}
td{border-bottom:1px solid #eee;padding:7px 8px;font-size:.88em}
.active{background:#fff}
.urgent{background:#fff8e1;border-left:3px solid #ff9800}
.claimed{background:#f9f9f9;color:#aaa;text-decoration:line-through}
.warn{background:#fff3e0}
.danger{background:#fce4ec;font-weight:bold}
.timedout{background:#ffcdd2;color:#b71c1c;font-weight:bold}
.timer-bar{height:5px;border-radius:2px;background:#e0e0e0;margin-top:3px;width:100%}
.timer-fill{height:5px;border-radius:2px;transition:width .5s}
#ts{font-size:.75em;color:#888}
</style></head><body>
<h1>&#128230; MMCall Collector Dashboard</h1>
<p>AP: <strong>MMCall-Collector</strong> &nbsp; IP: <strong>192.168.4.1</strong> &nbsp; <span id="ts"></span></p>
<div class="stats">
  <div class="stat"><span id="s-active">0</span>Active</div>
  <div class="stat"><span id="s-urgent">0</span>Urgent</div>
  <div class="stat"><span id="s-claimed">0</span>Claimed</div>
  <div class="stat"><span id="s-timed">0</span>Timed&nbsp;Out</div>
  <div class="stat"><span id="s-peers">0</span>Peers</div>
</div>
<table>
<thead><tr><th>Zone</th><th>Line</th><th>Part</th><th>Ordered</th><th>Elapsed</th><th>Status</th></tr></thead>
<tbody id="tbody"></tbody>
</table>
<p style="margin-top:12px">
<a href="/settings">&#9881; Settings &amp; Parts Config</a> &nbsp;|&nbsp;
<a href="/mesh">&#128246; Mesh Status</a> &nbsp;|&nbsp;
<a href="/history">&#128203; Shift History</a>
</p>
<script>
var TIMEOUT_MIN = 45;
function pct(elapsed) {
  var parts = elapsed.split(/[hms ]+/).filter(Boolean);
  var mins = 0, secs = 0;
  if (parts.length >= 2) { mins = parseInt(parts[0]); secs = parseInt(parts[1]); }
  else if (elapsed.indexOf('m') < 0) secs = parseInt(parts[0]);
  return Math.min(100, ((mins * 60 + secs) / (TIMEOUT_MIN * 60)) * 100);
}
function barColor(p) {
  if (p < 50) return '#4caf50';
  if (p < 75) return '#ff9800';
  return '#f44336';
}
function rowClass(o, p) {
  if (o.timedOut) return 'timedout';
  if (o.claimed)  return 'claimed';
  if (o.priority) return 'urgent';
  if (p >= 75)    return 'danger';
  if (p >= 50)    return 'warn';
  return 'active';
}
function refresh(){
  fetch('/api/active').then(r=>r.json()).then(d=>{
    var a=0,u=0,c=0,t=0;
    var rows='';
    d.orders.forEach(o=>{
      var p = pct(o.elapsed);
      var cls = rowClass(o, p);
      var statusLabel = o.timedOut ? '&#9888; TIMED OUT' : o.claimed ? '&#10003; Claimed' : (o.priority ? '&#9888; URGENT' : '&#9679; Active');
      var bar = '';
      if (!o.claimed && !o.timedOut) {
        bar = '<div class="timer-bar"><div class="timer-fill" style="width:'+p+'%;background:'+barColor(p)+'"></div></div>';
      }
      rows += '<tr class="'+cls+'"><td>Zone '+o.zone+'</td><td>'+o.lineID+'</td><td>'+o.part+'</td><td>'+o.timeOrdered+'</td><td>'+o.elapsed+bar+'</td><td>'+statusLabel+'</td></tr>';
      if (!o.claimed && !o.timedOut) { a++; if(o.priority) u++; }
      if (o.claimed) c++; if (o.timedOut) t++;
    });
    document.getElementById('tbody').innerHTML = rows || '<tr><td colspan="6" style="text-align:center;color:#888;padding:20px">No active orders</td></tr>';
    document.getElementById('s-active').textContent=a;
    document.getElementById('s-urgent').textContent=u;
    document.getElementById('s-claimed').textContent=c;
    document.getElementById('s-timed').textContent=t;
    document.getElementById('s-peers').textContent=d.peers||0;
    document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();
  });
}
setInterval(refresh,2000); refresh();
</script>
</body></html>
)rawliteral";

const char* settingsHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Settings</title>
<style>body{font-family:sans-serif;margin:20px;max-width:720px}
.card{background:#fff;border:1px solid #ddd;border-radius:6px;padding:16px;margin-bottom:16px}
h2{margin-top:0}label{display:block;font-weight:bold;margin-top:8px}
input,textarea,select{padding:5px;border:1px solid #ccc;border-radius:4px;margin:3px 0}
input[type=submit]{background:#1976d2;color:#fff;border:none;padding:8px 18px;border-radius:4px;cursor:pointer}
table{border-collapse:collapse;width:100%}td,th{border:1px solid #ddd;padding:6px;font-size:.88em}
</style></head><body>
<h1>&#9881; Collector Settings</h1>

<div class="card">
<h2>&#128246; Detected Devices (auto-discovered from mesh)</h2>
%DEVICE_TABLE%
<p style="font-size:.85em;color:#666">Devices appear automatically when they send a heartbeat. Use the form below to push a new parts list to any zone.</p>
</div>

<div class="card">
<h2>&#128230; Configure Parts for Zone</h2>
<form action="/api/config" method="post">
<label>Target Zone(s) (comma separated):</label>
<input name="zones" placeholder="1,2,3" size="20">
<label>Parts JSON:</label>
<textarea name="parts" rows="3" cols="55">["Pod Pickup","Empty Cart","Maintenance","Supervisor"]</textarea>
<br><br>
<input type="submit" value="Send Parts Config">
</form>
</div>

<div class="card">
<h2>&#128270; Mesh Tools</h2>
<form action="/api/catchup" method="post" style="display:inline">
<input type="submit" value="Rebroadcast All Active Orders">
</form>
&nbsp;
<form action="/api/timesync" method="post" style="display:inline">
<input type="submit" value="Broadcast Time Sync">
</form>
&nbsp;
<form action="/api/clear_claimed" method="post" style="display:inline">
<input type="submit" value="Archive &amp; Clear Claimed/Timed">
</form>
</div>

<div class="card">
<h2>&#128246; WiFi Client (for NTP time sync)</h2>
<p style="font-size:.85em;color:#666">Collector connects to your warehouse WiFi to sync real time via NTP. Time is then broadcast to all devices in the mesh.</p>
<form action="/api/setwifi" method="post">
<label>WiFi SSID:</label><input name="ssid" size="30" placeholder="WarehouseWiFi">
<label>Password:</label><input name="pass" type="password" size="30">
<br><br><input type="submit" value="Save &amp; Reconnect on Next Boot">
</form>
</div>

<p><a href="/">&#8592; Dashboard</a></p>
</body></html>
)rawliteral";

const char* meshHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Mesh Status</title>
<style>body{font-family:sans-serif;margin:20px}
table{border-collapse:collapse;width:100%}
th,td{border:1px solid #ddd;padding:8px;font-size:.88em}
th{background:#37474f;color:#fff}
.line{background:#e8f5e9}.tug{background:#e3f2fd}</style></head><body>
<h1>&#128246; Mesh Network Status</h1>
<table>
<thead><tr><th>Type</th><th>Label</th><th>Zone</th><th>RSSI</th><th>Hops</th><th>Last Seen</th></tr></thead>
<tbody id="tbody"></tbody>
</table>
<script>
fetch('/api/mesh').then(r=>r.json()).then(d=>{
  var rows='';
  d.peers.forEach(p=>{
    var cls=p.isLine?'line':'tug';
    var type=p.isLine?'Line Device':'Tugger/Other';
    rows+=`<tr class="${cls}"><td>${type}</td><td>${p.label}</td><td>Zone ${p.zone}</td><td>${p.rssi} dBm</td><td>${p.hops}</td><td>${p.ago}s ago</td></tr>`;
  });
  document.getElementById('tbody').innerHTML=rows||'<tr><td colspan="6" style="text-align:center">No peers</td></tr>';
});
</script>
<p><a href="/">&#8592; Dashboard</a></p>
</body></html>
)rawliteral";

const char* historyHTML PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Shift History</title>
<style>body{font-family:sans-serif;margin:20px}
table{border-collapse:collapse;width:100%}
th,td{border:1px solid #ddd;padding:8px;font-size:.88em}th{background:#37474f;color:#fff}
.ok{color:green}.late{color:red}</style></head><body>
<h1>&#128203; Shift History</h1>
<table>
<thead><tr><th>Line</th><th>Part</th><th>Zone</th><th>Ordered</th><th>Response Time</th><th>Result</th></tr></thead>
<tbody id="tbody"></tbody>
</table>
<form action="/api/clear_history" method="post" style="margin-top:12px">
<input type="submit" value="Clear History" style="padding:6px 14px">
</form>
<p><a href="/">&#8592; Dashboard</a></p>
<script>
fetch('/api/history').then(r=>r.json()).then(d=>{
  var rows='';
  d.history.forEach(h=>{
    var cls=h.claimed?'ok':'late';
    var res=h.claimed?'&#10003; Claimed':'&#9888; Timed Out';
    rows+=`<tr><td>${h.lineID}</td><td>${h.part}</td><td>Zone ${h.zone}</td><td>${h.timeOrdered}</td><td>${h.elapsed}</td><td class="${cls}">${res}</td></tr>`;
  });
  document.getElementById('tbody').innerHTML=rows||'<tr><td colspan="6" style="text-align:center">No history</td></tr>';
});
</script>
</body></html>
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
            unsigned long elapsed = (millis() - activeOrders[i].timestamp) / 1000;
            o["elapsed"] = String(elapsed / 60) + "m " + String(elapsed % 60) + "s";
            o["status"]  = activeOrders[i].claimed  ? "Claimed" :
                           activeOrders[i].timedOut ? "TIMED OUT" : "Active";
            o["claimed"]  = activeOrders[i].claimed;
            o["timedOut"] = activeOrders[i].timedOut;
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
                Serial.printf("[WIFI] Credentials saved: %s\n", ssid.c_str());
            }
        }
        server.sendHeader("Location", "/settings"); server.send(303);
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
    if (state != RADIOLIB_ERR_NONE) { Serial.printf("[RADIO] Failed: %d\n", state); while(true); }
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
            Serial.printf("[NTP] Synced: %s\n", timeClient.getFormattedTime().c_str());
            broadcastTimeSync();
        }
    }

    delay(300);
    broadcastCatchup();

    Serial.printf("[BOOT] Collector ready ID=0x%08X\n", myDeviceID);
    smartUpdateDisplay();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
    server.handleClient();

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
                        Serial.printf("[CALL] Dedup update for %s|%s\n",
                            lineStr.c_str(), partStr.c_str());
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
                        Serial.printf("[CALL] %s|%s Z%d t=%s\n",
                            lineStr.c_str(), partStr.c_str(), pkt.route,
                            activeOrders[idx].timeOrdered);
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
                    Serial.printf("[BOOT_REQ] From 0x%08X — sending catchup+timesync\n", pkt.srcID);
                    // startReceive called after the outer if-block — fine.
                    // Use short delay only so booting device is ready to receive.
                    delay(150);
                    broadcastTimeSync();
                    delay(50);
                    broadcastCatchup();
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
                Serial.printf("[TIMEOUT] %s|%s Z%d ordered=%s\n",
                    activeOrders[i].lineID, activeOrders[i].part,
                    activeOrders[i].zone, activeOrders[i].timeOrdered);
            }
        }
        if (changed) { saveOrdersToNVS(); smartUpdateDisplay(); }
    }
}
