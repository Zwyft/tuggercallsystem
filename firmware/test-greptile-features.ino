// Test file for Greptile feature detection
// This file contains various patterns to test Greptile's analysis capabilities

#include <Arduino.h>
#include <Preferences.h>

// Test 1: Memory allocation patterns (potential memory leak)
class TestClass {
public:
    TestClass() { data = new int[100]; } // Memory allocation
    ~TestClass() { delete[] data; } // Proper cleanup
private:
    int* data;
};

// Test 2: Buffer operations (potential buffer overflow)
void testBufferOperations() {
    char buffer[10];
    String input = "This is a very long string that could cause buffer overflow";
    
    // Potential buffer overflow - Greptile should detect this
    input.toCharArray(buffer, 10); // ❌ Dangerous - no bounds checking
    
    // Safe version - Greptile should recognize this as good practice
    input.toCharArray(buffer, sizeof(buffer)); // ✅ Safe with bounds checking
}

// Test 3: Race conditions (potential thread safety)
volatile bool sharedFlag = false;
void threadFunction() {
    // Potential race condition - Greptile should detect this
    sharedFlag = true; // No synchronization
}

// Test 4: Null pointer dereference (potential crash)
void testNullPointer() {
    String* ptr = nullptr;
    // Potential null pointer dereference - Greptile should detect this
    if(ptr->length() > 0) { // ❌ Dangerous - could crash
        // Safe version - Greptile should recognize this
        if(ptr != nullptr && ptr->length() > 0) { // ✅ Safe null check
            // Code here
        }
    }
}

// Test 5: Infinite loop risk (potential system hang)
void testInfiniteLoop() {
    // Potential infinite loop - Greptile should detect this
    while(true) {
        // No exit condition - could hang system
        delay(1000);
    }
    
    // Safe version - Greptile should recognize this
    unsigned long timeout = millis() + 10000;
    while(millis() < timeout) {
        delay(100);
        // Exit condition exists
    }
}

// Test 6: Resource management
void testResourceManagement() {
    Preferences prefs;
    prefs.begin("test", false);
    
    // Resource allocation without proper cleanup
    // Greptile should detect potential resource leak
    prefs.putString("test", "value");
    // prefs.end() is missing here - potential resource leak
}

void setup() {
    Serial.begin(115200);
    
    // Test various patterns
    TestClass test;
    testBufferOperations();
    threadFunction();
    testNullPointer();
    testInfiniteLoop();
    testResourceManagement();
    
    Serial.println("Greptile test patterns initialized");
}

void loop() {
    // Main loop - should be safe
    delay(1000);
}
