#include "input_helper.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Include Mocks
#include "mocks/Arduino.h"
#include "mocks/ArduinoJson.h"
#include "mocks/Bounce2.h"
#include "mocks/ElegantOTA.h"
#include "mocks/HTTPClient.h"
#include "mocks/Preferences.h"
#include "mocks/RadioLib.h"
#include "mocks/SPI.h"
#include "mocks/WiFi.h"
#include "mocks/heltec-eink-modules.h"

// Arduino.h externs
MockSerial Serial;
MockESP ESP;
MockWiFi WiFi;
MockSPI SPI;

// Forward Decl for .ino functions
void updateDisplay();
void sendToDashboard(int idx);

// E213 has `setup()` and `loop()`.
// E213 File Path relative to here:
// ../TuggerRouteE213Final_withhash/TuggerRouteE213Final_withhash.ino

#include "../TuggerRouteE213Final_withhash/TuggerRouteE213Final_withhash.ino"

// Interactive Helper
void runInteractive() {
  std::cout << "\n============================================\n";
  std::cout << "   T U G R   E 2 1 3   S I M U L A T O R    \n";
  std::cout << "============================================\n";
  std::cout << " NAV:     [W] Up    [S] Down   [E] Select/Ack\n";
  std::cout << " SYSTEM:  [M] Mode  [C] Clear\n";
  std::cout << " LINE:    [1] A     [2] B      [3] C      [4] D\n";
  std::cout << " RADIO:   [R] Rx Order [K] Rx Ack [A] Ack Delivery\n";
  std::cout << " MESH:    [F] Foreign (Relay) [D] Duplicate [Z] All-Call\n";
  std::cout << " BROWSER: [B] Sim Browser Login\n";
  std::cout << " [Q] Quit\n";
  std::cout << "--------------------------------------------\n";
  std::cout << "Simulating... (Press keys to interact)\n";

  // Boot
  // Simulate Released Select Button (Pin 21) for Normal Boot
  mockPinStates[21] = 1; // HIGH
  setup();

  while (true) {
    // Non-blocking input check
    char inputChar = 0;
    if (checkInput(inputChar)) {
      char input = tolower(inputChar);

      bool action = false;

      switch (input) {
      case 'w':
        debouncers[7].mockPress();
        std::cout << "[KEY] UP\n";
        action = true;
        break;
      case 's':
        debouncers[8].mockPress();
        std::cout << "[KEY] DOWN\n";
        action = true;
        break;
      case 'e':
        debouncers[1].mockPress();
        std::cout << "[KEY] SELECT\n";
        action = true;
        break;
      case 'm':
        debouncers[0].mockPress();
        std::cout << "[KEY] MODE\n";
        action = true;
        break;
      case 'c':
        debouncers[6].mockPress();
        std::cout << "[KEY] CLEAR\n";
        action = true;
        break;
      case '1':
        debouncers[2].mockPress();
        std::cout << "[KEY] LINE A\n";
        action = true;
        break;
      case '2':
        debouncers[3].mockPress();
        std::cout << "[KEY] LINE B\n";
        action = true;
        break;
      case '3':
        debouncers[4].mockPress();
        std::cout << "[KEY] LINE C\n";
        action = true;
        break;
      case '4':
        debouncers[5].mockPress();
        std::cout << "[KEY] LINE D\n";
        action = true;
        break;

      case 'r': { // Rx Order
        std::vector<uint8_t> buf(104, 0);
        buf[0] = 1; // PKT_ORDER
        uint32_t src = 999;
        memcpy(&buf[1], &src, 4);
        uint32_t dst = selectedZone;
        memcpy(&buf[5], &dst, 4);
        uint32_t oid = 12345;
        memcpy(&buf[9], &oid, 4);
        const char *line = "Sim Line";
        const char *part = "Sim Part";
        strcpy((char *)&buf[14], line);
        strcpy((char *)&buf[42], part);
        buf[102] = 0;
        buf[103] = 0;
        uint16_t crc = calcChecksum(buf.data(), 104);
        memcpy(&buf[102], &crc, 2);

        std::cout << "\n[SIM] Injecting Order for Zone " << selectedZone
                  << "\n";
        radio.mockReceiveBinary(buf);
        action = true;
        break;
      }
      case 'k': { // Rx Ack
        std::cout << "\n[SIM] Note: Key 'K' (Rx Ack) is reserved for creating "
                     "'PKT_ACK' (Human Accepted).\n";
        break;
      }

      case 'a': { // Ack Delivery (Network ACK)
        if (lastRequest.orderId == 0) {
          std::cout << "\n[SIM] No active request to ACK.\n";
          break;
        }
        std::vector<uint8_t> buf(104, 0);
        buf[0] = 4;         // PKT_DELIVERED
        uint32_t src = 999; // Mock Tugger
        memcpy(&buf[1], &src, 4);
        uint32_t dst = selectedZone; // Back to us
        memcpy(&buf[5], &dst, 4);
        uint32_t oid = lastRequest.orderId; // ACK the specific order
        memcpy(&buf[9], &oid, 4);

        uint16_t crc = calcChecksum(buf.data(), 104);
        memcpy(&buf[102], &crc, 2);

        std::cout << "\n[SIM] Injecting Delivery ACK for Order " << oid << "\n";
        radio.mockReceiveBinary(buf);
        action = true;
        break;
      }

      case 'f': { // FOREIGN PACKET (Test Relay)
        std::vector<uint8_t> buf(104, 0);
        buf[0] = 1; // PKT_ORDER
        uint32_t src = 888;
        memcpy(&buf[1], &src, 4);
        uint32_t dst = 99; // Foreign Zone (Not Us)
        memcpy(&buf[5], &dst, 4);
        uint32_t oid = 99999;
        memcpy(&buf[9], &oid, 4);
        uint8_t hop = 0;
        memcpy(&buf[13], &hop, 1); // Hop Count

        const char *line = "Foreign Line";
        const char *part = "Part X";
        strcpy((char *)&buf[14], line);
        strcpy((char *)&buf[42], part);
        buf[102] = 0;
        buf[103] = 0;
        uint16_t crc = calcChecksum(buf.data(), 104);
        memcpy(&buf[102], &crc, 2);

        std::cout << "\n[SIM] Injecting Foreign Order (Zone 99)\n";
        radio.mockReceiveBinary(buf);
        action = true;
        break;
      }

      case 'd': { // DUPLICATE PACKET (Test Dedup)
        std::vector<uint8_t> buf(104, 0);
        buf[0] = 1; // PKT_ORDER
        uint32_t src = 777;
        memcpy(&buf[1], &src, 4);
        uint32_t dst = selectedZone; // For Us
        memcpy(&buf[5], &dst, 4);
        uint32_t oid = 55555; // Fixed ID
        memcpy(&buf[9], &oid, 4);
        uint8_t hop = 1;
        memcpy(&buf[13], &hop, 1);

        const char *line = "Dedup Line";
        const char *part = "Part Y";
        strcpy((char *)&buf[14], line);
        strcpy((char *)&buf[42], part);
        buf[102] = 0;
        buf[103] = 0;
        uint16_t crc = calcChecksum(buf.data(), 104);
        memcpy(&buf[102], &crc, 2);

        std::cout << "\n[SIM] Injecting Duplicate Packet (ID 55555)\n";
        radio.mockReceiveBinary(buf);
        action = true;
        break;
      }

      case 'z': { // ALL-CALL (Zone 0)
        std::vector<uint8_t> buf(104, 0);
        buf[0] = 1; // PKT_ORDER
        uint32_t src = 666;
        memcpy(&buf[1], &src, 4);
        uint32_t dst = 0; // Zone 0 (All Call)
        memcpy(&buf[5], &dst, 4);
        uint32_t oid = 11111;
        memcpy(&buf[9], &oid, 4);

        const char *line = "EMERGENCY";
        const char *part = "STOP LINE";
        strcpy((char *)&buf[14], line);
        strcpy((char *)&buf[42], part);

        uint16_t crc = calcChecksum(buf.data(), 104);
        memcpy(&buf[102], &crc, 2);

        std::cout << "\n[SIM] Injecting All-Call (Zone 0)\n";
        radio.mockReceiveBinary(buf);
        action = true;
        break;
      }

      case 'b': { // BROWSER SIMULATION
        std::cout << "\n[SIM] Opening Browser (Simulating Config Mode)...\n";

        // Initialize Server & OTA as if we entered runConfigMode
        initWebServer();
        ElegantOTA.begin(&server);

        // 1. Visit Root
        server.mockRequest("", "", "/");

        // 2. Login (New JSON API)
        std::string codeInput = "0000";
        std::cout << "[SIM] Auto-entering Pairing Code: " << codeInput << "\n";

        // Mock JSON body
        std::string jsonBody = "{\"code\":\"" + codeInput + "\"}";
        server.mockApiRequest("/api/login", jsonBody.c_str());

        if (isAuthenticated) {
          std::cout
              << "[SIM] Authenticated! Changing Zone to 5 and Button 1 to "
                 "'TEST LINE'...\n";

          // Mock JSON Config Update
          std::string configJson =
              "{\"zone\":5, \"b1l\":\"TEST LINE\", \"b1p\":\"partXYZ\"}";
          server.mockApiRequest("/api/config", configJson.c_str());
        }
        action = true;
        break;
      }
      case 'x': { // CRASH (Simulate Panic)
        std::ofstream f("sim_reset_reason.dat");
        f << 4; // ESP_RST_PANIC
        f.close();
        printf("\n[SIM] TRIGGERING PANIC... (Simulating Reboot)\n");
        exit(0);
      }

      case 't': { // TIME TRAVEL (Age first order by 50 mins)
        if (orderCount > 0) {
          orders[0].createdTime = millis() - (50 * 60 * 1000);
          printf("\n[SIM] Aged Order #%d by 50 mins (Triggers RED Alert)\n",
                 orders[0].orderId);
          updateDisplay();
        } else {
          printf("\n[SIM] No orders to age.\n");
        }
        action = true;
        break;
      }

      case 'l': { // LIST LOGS (Simulate Browser viewing logs)
        // In a real browser we'd fetch /api/state or /api/log_timeout (POST).
        // Here we can just print the NVS file content for 'kpi'.
        // Or just call the API handler if we want to test that?
        // Let's print NVS for quick verification.
        printf("\n[SIM] KPI LOGS (NVS):\n");
        // Actually, we can't easily read NVS internal map from here without
        // exposing it. Let's use the browser mock approach:
        std::cout << "[SIM] Fetching State (to see timeouts)...\n";
        server.mockRequest("", "", "/api/state");
        break;
      }

      case 'q':
        exit(0);
      }
    }

    loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// Stub for Tests
void runTests() {
  setup();
  loop();
}

int main(int argc, char *argv[]) {
  try {
    runInteractive();
  } catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
