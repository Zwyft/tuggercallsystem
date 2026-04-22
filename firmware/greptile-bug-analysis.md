# Greptile Bug Analysis Report

## Summary
- Total bugs found: 99
- High severity: 11
- Medium severity: 88

## 🚨 High Severity Bugs

### Potential memory leak
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 165
**Code:** `SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);`
**Description:** Line 165: SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI); - Missing corresponding delete/free

### Potential memory leak
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 680
**Code:** `document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();`
**Description:** Line 680: document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString(); - Missing corresponding delete/free

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 672
**Code:** `if (PIN_LED >= 0) digitalWrite(PIN_LED, HIGH);`
**Description:** Line 672: if (PIN_LED >= 0) digitalWrite(PIN_LED, HIGH);

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 673
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 673: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); }

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 714
**Code:** `if (pkt->configStr[0] == '[') {`
**Description:** Line 714: if (pkt->configStr[0] == '[') {

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 874
**Code:** `if (PIN_LED    >= 0) { pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW); }`
**Description:** Line 874: if (PIN_LED    >= 0) { pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW); }

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 912
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 912: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); }

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 1038
**Code:** `if (!anyCallActive && PIN_LED >= 0) digitalWrite(PIN_LED, LOW);`
**Description:** Line 1038: if (!anyCallActive && PIN_LED >= 0) digitalWrite(PIN_LED, LOW);

### Potential null pointer dereference
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 1039
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 1039: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); }

### Potential memory leak
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 195
**Code:** `SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);`
**Description:** Line 195: SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI); - Missing corresponding delete/free

### Potential memory leak
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 206
**Code:** `SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI);`
**Description:** Line 206: SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY, SPI); - Missing corresponding delete/free

## ⚠️ Medium Severity Bugs

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 263
**Code:** `strncpy(neighbors[i].label, label, 27);`
**Description:** Line 263: strncpy(neighbors[i].label, label, 27); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 278
**Code:** `strncpy(neighbors[i].label, label, 27);`
**Description:** Line 278: strncpy(neighbors[i].label, label, 27); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 342
**Code:** `strncpy(activeOrders[idx].lineID, o["line"] | "", 23);`
**Description:** Line 342: strncpy(activeOrders[idx].lineID, o["line"] | "", 23); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 344
**Code:** `strncpy(activeOrders[idx].part,   o["part"] | "", 23);`
**Description:** Line 344: strncpy(activeOrders[idx].part,   o["part"] | "", 23); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 346
**Code:** `strncpy(activeOrders[idx].timeOrdered, o["time"] | "--:--", 5);`
**Description:** Line 346: strncpy(activeOrders[idx].timeOrdered, o["time"] | "--:--", 5); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 474
**Code:** `strncpy(start.configStr, "[", 19); start.configStr[19] = '\0';`
**Description:** Line 474: strncpy(start.configStr, "[", 19); start.configStr[19] = '\0'; - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 925
**Code:** `strncpy(history[h].lineID, activeOrders[i].lineID, 23);`
**Description:** Line 925: strncpy(history[h].lineID, activeOrders[i].lineID, 23); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 927
**Code:** `strncpy(history[h].part,   activeOrders[i].part,   23);`
**Description:** Line 927: strncpy(history[h].part,   activeOrders[i].part,   23); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 929
**Code:** `strncpy(history[h].timeOrdered, activeOrders[i].timeOrdered, 5);`
**Description:** Line 929: strncpy(history[h].timeOrdered, activeOrders[i].timeOrdered, 5); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1084
**Code:** `strncpy(peerLabel, pkt.item, 27);`
**Description:** Line 1084: strncpy(peerLabel, pkt.item, 27); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1085
**Code:** `peerLabel[27] = '\0'; // Explicit null — strncpy(27) doesn't guarantee byte[27]`
**Description:** Line 1085: peerLabel[27] = '\0'; // Explicit null — strncpy(27) doesn't guarantee byte[27] - Missing bounds checking

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 400
**Code:** `txEnqueue(p); radio.startReceive(); return;`
**Description:** Line 400: txEnqueue(p); radio.startReceive(); return; - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 403
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket));`
**Description:** Line 403: radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket)); - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 405
**Code:** `radio.startReceive();`
**Description:** Line 405: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 451
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(&pkt), sizeof(MeshPacket));`
**Description:** Line 451: radio.transmit(reinterpret_cast<uint8_t*>(&pkt), sizeof(MeshPacket)); - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 454
**Code:** `radio.startReceive();`
**Description:** Line 454: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 509
**Code:** `void updateDisplay() {`
**Description:** Line 509: void updateDisplay() { - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1011
**Code:** `radio.startReceive();`
**Description:** Line 1011: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1064
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]),`
**Description:** Line 1064: radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]), - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1070
**Code:** `radio.startReceive();`
**Description:** Line 1070: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1182
**Code:** `radio.startReceive();`
**Description:** Line 1182: radio.startReceive(); - Check for race conditions

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 394
**Code:** `delay(random(20, 120));`
**Description:** Line 394: delay(random(20, 120)); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 397
**Code:** `delay(random(50, 180));`
**Description:** Line 397: delay(random(50, 180)); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 456
**Code:** `delay(random(60, 120)); // Inter-packet spacing`
**Description:** Line 456: delay(random(60, 120)); // Inter-packet spacing - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 476
**Code:** `delay(80);`
**Description:** Line 476: delay(80); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 489
**Code:** `delay(80);`
**Description:** Line 489: delay(80); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1023
**Code:** `while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) delay(100);`
**Description:** Line 1023: while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) delay(100); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1032
**Code:** `delay(300);`
**Description:** Line 1032: delay(300); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1061
**Code:** `// transmitMesh() would add 20-300ms of delay() which freezes handleClient()`
**Description:** Line 1061: // transmitMesh() would add 20-300ms of delay() which freezes handleClient() - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1175
**Code:** `delay(150);`
**Description:** Line 1175: delay(150); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino
**Line:** 1177
**Code:** `delay(50);`
**Description:** Line 1177: delay(50); - Check for proper loop termination

### Potential buffer overflow
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 327
**Code:** `if (numCallTypes == 0) { strncpy(callTypes[0].label,"Pickup",23); callTypes[0].active=false; callTypes[0].priority=PRIORITY_NORMAL; callTypes[0].clearedAt=0; numCallTypes=1; }`
**Description:** Line 327: if (numCallTypes == 0) { strncpy(callTypes[0].label,"Pickup",23); callTypes[0].active=false; callTypes[0].priority=PRIORITY_NORMAL; callTypes[0].clearedAt=0; numCallTypes=1; } - Missing bounds checking

### Potential buffer overflow
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 723
**Code:** `strncpy(callTypes[numCallTypes].label, pkt->configStr, 23);`
**Description:** Line 723: strncpy(callTypes[numCallTypes].label, pkt->configStr, 23); - Missing bounds checking

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 503
**Code:** `void updateDisplay() {`
**Description:** Line 503: void updateDisplay() { - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 629
**Code:** `txEnqueue(p); radio.startReceive(); return;`
**Description:** Line 629: txEnqueue(p); radio.startReceive(); return; - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 632
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket));`
**Description:** Line 632: radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket)); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 634
**Code:** `radio.startReceive();`
**Description:** Line 634: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 666
**Code:** `transmitMesh(&pkt); // transmitMesh calls radio.startReceive() internally`
**Description:** Line 666: transmitMesh(&pkt); // transmitMesh calls radio.startReceive() internally - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 672
**Code:** `if (PIN_LED >= 0) digitalWrite(PIN_LED, HIGH);`
**Description:** Line 672: if (PIN_LED >= 0) digitalWrite(PIN_LED, HIGH); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 673
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 673: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); } - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 686
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(&ack), sizeof(OtaPacket));`
**Description:** Line 686: radio.transmit(reinterpret_cast<uint8_t*>(&ack), sizeof(OtaPacket)); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 687
**Code:** `radio.startReceive();`
**Description:** Line 687: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 848
**Code:** `radio.startReceive();`
**Description:** Line 848: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 874
**Code:** `if (PIN_LED    >= 0) { pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW); }`
**Description:** Line 874: if (PIN_LED    >= 0) { pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED, LOW); } - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 912
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 912: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); } - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 937
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]),`
**Description:** Line 937: radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]), - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 943
**Code:** `radio.startReceive(); // Always restore receive mode after drain attempt`
**Description:** Line 943: radio.startReceive(); // Always restore receive mode after drain attempt - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 1038
**Code:** `if (!anyCallActive && PIN_LED >= 0) digitalWrite(PIN_LED, LOW);`
**Description:** Line 1038: if (!anyCallActive && PIN_LED >= 0) digitalWrite(PIN_LED, LOW); - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 1039
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 1039: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); } - Check for race conditions

### Potential race condition
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 1083
**Code:** `radio.startReceive();`
**Description:** Line 1083: radio.startReceive(); - Check for race conditions

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 622
**Code:** `delay(random(20, 120));`
**Description:** Line 622: delay(random(20, 120)); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 626
**Code:** `delay(random(50, 180));`
**Description:** Line 626: delay(random(50, 180)); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 673
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 673: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(200); digitalWrite(PIN_BUZZER, LOW); } - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 738
**Code:** `delay(300);`
**Description:** Line 738: delay(300); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 782
**Code:** `delay(10);`
**Description:** Line 782: delay(10); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 797
**Code:** `delay(350);`
**Description:** Line 797: delay(350); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 845
**Code:** `while (true) delay(1000);`
**Description:** Line 845: while (true) delay(1000); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 855
**Code:** `while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) delay(100);`
**Description:** Line 855: while (WiFi.status() != WL_CONNECTED && millis() - t < 3000) delay(100); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 899
**Code:** `while (confirmBtn.read() == LOW) { confirmBtn.update(); delay(10); }`
**Description:** Line 899: while (confirmBtn.read() == LOW) { confirmBtn.update(); delay(10); } - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 912
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 912: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); } - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 933
**Code:** `// transmitMesh() carries its own delay(random) which would block`
**Description:** Line 933: // transmitMesh() carries its own delay(random) which would block - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 998
**Code:** `delay(500); ESP.restart();`
**Description:** Line 998: delay(500); ESP.restart(); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/LineDeviceFirmware/LineDeviceFirmware.ino
**Line:** 1039
**Code:** `if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); }`
**Description:** Line 1039: if (PIN_BUZZER >= 0) { digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW); } - Check for proper loop termination

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 289
**Code:** `if (label) { strncpy(neighbors[i].label, label, 15); neighbors[i].label[15] = '\0'; }`
**Description:** Line 289: if (label) { strncpy(neighbors[i].label, label, 15); neighbors[i].label[15] = '\0'; } - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 297
**Code:** `if (label) { strncpy(neighbors[i].label, label, 15); neighbors[i].label[15] = '\0'; }`
**Description:** Line 297: if (label) { strncpy(neighbors[i].label, label, 15); neighbors[i].label[15] = '\0'; } - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 391
**Code:** `strncpy(activeCalls[s].item, o["item"] | "", 27);`
**Description:** Line 391: strncpy(activeCalls[s].item, o["item"] | "", 27); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 393
**Code:** `strncpy(activeCalls[s].timeOrdered, o["time"] | "--:--", 5);`
**Description:** Line 393: strncpy(activeCalls[s].timeOrdered, o["time"] | "--:--", 5); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 440
**Code:** `strncpy(clearedCache[oldest].item, item, 27);`
**Description:** Line 440: strncpy(clearedCache[oldest].item, item, 27); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 477
**Code:** `strncpy(activeCalls[i].item, item, 27);`
**Description:** Line 477: strncpy(activeCalls[i].item, item, 27); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 478
**Code:** `strncpy(activeCalls[i].timeOrdered, timeStr, 5);`
**Description:** Line 478: strncpy(activeCalls[i].timeOrdered, timeStr, 5); - Missing bounds checking

### Potential buffer overflow
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 514
**Code:** `strncpy(claim.item, activeCalls[index].item, 27); claim.item[27] = '\0';`
**Description:** Line 514: strncpy(claim.item, activeCalls[index].item, 27); claim.item[27] = '\0'; - Missing bounds checking

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 338
**Code:** `radio.startReceive();`
**Description:** Line 338: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 343
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket));`
**Description:** Line 343: radio.transmit(reinterpret_cast<uint8_t*>(p), sizeof(MeshPacket)); - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 345
**Code:** `radio.startReceive();`
**Description:** Line 345: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 568
**Code:** `void updateDisplay() {`
**Description:** Line 568: void updateDisplay() { - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 715
**Code:** `delay(300); // Reduced from 700ms to minimise deaf window after radio.startReceive()`
**Description:** Line 715: delay(300); // Reduced from 700ms to minimise deaf window after radio.startReceive() - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 772
**Code:** `// Stagger boot request BEFORE radio.startReceive() so the delay`
**Description:** Line 772: // Stagger boot request BEFORE radio.startReceive() so the delay - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 775
**Code:** `radio.startReceive();`
**Description:** Line 775: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 797
**Code:** `radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]),`
**Description:** Line 797: radio.transmit(reinterpret_cast<uint8_t*>(&txQueue[txQHead]), - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 803
**Code:** `radio.startReceive();`
**Description:** Line 803: radio.startReceive(); - Check for race conditions

### Potential race condition
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 898
**Code:** `radio.startReceive();`
**Description:** Line 898: radio.startReceive(); - Check for race conditions

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 326
**Code:** `delay(random(20, 150));`
**Description:** Line 326: delay(random(20, 150)); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 333
**Code:** `delay(random(50, 200));`
**Description:** Line 333: delay(random(50, 200)); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 526
**Code:** `delay(random(80, 180)); // spacing — ensures first TX is on air first`
**Description:** Line 526: delay(random(80, 180)); // spacing — ensures first TX is on air first - Check for proper loop termination

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 690
**Code:** `delay(10);`
**Description:** Line 690: delay(10); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 715
**Code:** `delay(300); // Reduced from 700ms to minimise deaf window after radio.startReceive()`
**Description:** Line 715: delay(300); // Reduced from 700ms to minimise deaf window after radio.startReceive() - Check for proper loop termination

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 765
**Code:** `while (true) delay(1000);`
**Description:** Line 765: while (true) delay(1000); - Check for proper loop termination

### Potential infinite loop
**File:** firmware/TuggerDeviceFirmware/TuggerFirmware.ino
**Line:** 774
**Code:** `delay(random(100, 400));`
**Description:** Line 774: delay(random(100, 400)); - Check for proper loop termination

## Recommendations
1. Review all high severity bugs immediately
2. Implement proper bounds checking for array operations
3. Add race condition protection for shared resources
4. Ensure proper memory management
5. Test all loops for proper termination
