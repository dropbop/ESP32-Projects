#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "secrets.h"  // WiFi creds and API token
#include "forced_calibration.h"

// Config (not sensitive - safe to commit)
const char* endpoint = "https://www.dropbop.xyz/api/sensor";
const char* eventEndpoint = "https://www.dropbop.xyz/api/sensor/log";
const char* deviceName = "office";

// Timing config - adjust as needed
const unsigned long MEASUREMENT_INTERVAL_MS = 60000;  // 60 seconds - matches sensor response time
const unsigned long WIFI_RETRY_DELAY_MS = 500;
const int WIFI_MAX_ATTEMPTS = 30;

// LED for status
#define LED_PIN 2

// Sensor
SensirionI2cScd4x sensor;

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

static char errorMessage[64];
static int16_t error;

// Track consecutive failures for diagnostics
static uint32_t consecutiveFailures = 0;
static uint32_t consecutiveI2CFailures = 0;
static uint32_t totalMeasurements = 0;
static uint32_t successfulUploads = 0;
static uint32_t totalI2CErrors = 0;
static uint32_t totalWiFiReconnects = 0;

// Event types
enum EventType {
    EVENT_INFO,
    EVENT_WARNING,
    EVENT_ERROR,
    EVENT_CRITICAL
};

void flashLED(int times, int duration = 100) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(duration);
        digitalWrite(LED_PIN, LOW);
        if (i < times - 1) delay(duration);
    }
}

// Send event/error to server for logging
bool sendEvent(EventType type, const char* message) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[EVENT NOT SENT - NO WIFI] %s\n", message);
        return false;
    }
    
    const char* typeStr;
    switch (type) {
        case EVENT_INFO:     typeStr = "info"; break;
        case EVENT_WARNING:  typeStr = "warning"; break;
        case EVENT_ERROR:    typeStr = "error"; break;
        case EVENT_CRITICAL: typeStr = "critical"; break;
        default:             typeStr = "info"; break;
    }
    
    HTTPClient http;
    http.begin(eventEndpoint);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Sensor-Token", sensorToken);
    http.setTimeout(5000);  // 5 second timeout for event logging
    
    String payload = "{\"device\":\"" + String(deviceName) + 
                     "\",\"event_type\":\"" + String(typeStr) + 
                     "\",\"message\":\"" + String(message) + 
                     "\",\"uptime\":" + String(millis() / 1000) +
                     ",\"heap\":" + String(ESP.getFreeHeap()) +
                     ",\"total_measurements\":" + String(totalMeasurements) +
                     ",\"i2c_errors\":" + String(totalI2CErrors) + "}";
    
    Serial.print("[EVENT] Sending: ");
    Serial.println(payload);
    
    int httpCode = http.POST(payload);
    http.end();
    
    return httpCode == 200;
}

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_ATTEMPTS) {
        delay(WIFI_RETRY_DELAY_MS);
        Serial.print(".");
        // Slow blink while connecting
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        attempts++;
    }
    digitalWrite(LED_PIN, LOW);
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("WiFi connection failed!");
        // Rapid blink for WiFi failure
        for (int i = 0; i < 10; i++) {
            flashLED(1, 50);
            delay(50);
        }
    }
}

bool sendData(uint16_t co2, float temp, float humidity) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, attempting reconnect...");
        totalWiFiReconnects++;
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Still no WiFi, skipping upload.");
            flashLED(3);  // Triple flash = failed
            return false;
        }
        // Log successful reconnection
        sendEvent(EVENT_WARNING, "WiFi reconnected after disconnect");
    }
    
    HTTPClient http;
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Sensor-Token", sensorToken);
    
    String payload = "{\"device\":\"" + String(deviceName) + 
                     "\",\"co2\":" + String(co2) + 
                     ",\"temp\":" + String(temp, 1) + 
                     ",\"humidity\":" + String(humidity, 1) + "}";
    
    Serial.print("Sending: ");
    Serial.println(payload);
    
    int httpCode = http.POST(payload);
    bool success = false;
    
    if (httpCode == 200) {
        Serial.print("HTTP Response: ");
        Serial.println(httpCode);
        String response = http.getString();
        Serial.println(response);
        flashLED(1);  // Single flash = success
        success = true;
    } else {
        Serial.print("HTTP Error: ");
        if (httpCode > 0) {
            Serial.println(httpCode);
        } else {
            Serial.println(http.errorToString(httpCode));
        }
        flashLED(3);  // Triple flash = failed
    }
    
    http.end();
    return success;
}

void printDiagnostics() {
    Serial.println("=== Diagnostics ===");
    Serial.printf("Total measurements: %lu\n", totalMeasurements);
    Serial.printf("Successful uploads: %lu\n", successfulUploads);
    Serial.printf("Success rate: %.1f%%\n", 
        totalMeasurements > 0 ? (100.0 * successfulUploads / totalMeasurements) : 0.0);
    Serial.printf("Consecutive failures: %lu\n", consecutiveFailures);
    Serial.printf("Total I2C errors: %lu\n", totalI2CErrors);
    Serial.printf("WiFi reconnects: %lu\n", totalWiFiReconnects);
    Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    Serial.println("==================");
}

// Attempt to recover I2C bus and sensor
bool recoverI2C() {
    Serial.println("Attempting I2C recovery...");
    
    // Toggle I2C pins to try to reset the bus
    Wire.end();
    delay(100);
    
    // Manually clock out any stuck transaction
    pinMode(21, OUTPUT);  // SDA
    pinMode(22, OUTPUT);  // SCL
    
    digitalWrite(21, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(22, HIGH);
        delayMicroseconds(5);
        digitalWrite(22, LOW);
        delayMicroseconds(5);
    }
    
    // Generate STOP condition
    digitalWrite(21, LOW);
    delayMicroseconds(5);
    digitalWrite(22, HIGH);
    delayMicroseconds(5);
    digitalWrite(21, HIGH);
    
    delay(100);
    
    // Reinitialize I2C
    Wire.begin(21, 22);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    
    delay(50);
    
    // Try to wake up and verify sensor
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        return false;
    }
    
    delay(30);
    
    // Verify by getting serial number
    uint64_t serialNumber = 0;
    error = sensor.getSerialNumber(serialNumber);
    if (error != NO_ERROR) {
        return false;
    }
    
    Serial.println("I2C recovery successful!");
    return true;
}

void setup() {
    Serial.begin(115200);
    
    // Init LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    while (!Serial) {
        delay(100);
    }
    
    Serial.println();
    Serial.println("=== ESP32 SCD41 CO2 Sensor + WiFi ===");
    Serial.println("Single-shot mode - 60 second intervals");
    
    // Connect to WiFi
    connectWiFi();
    
    // Initialize I2C
    Wire.begin(21, 22);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    
    delay(30);  // Wait for sensor power-up (datasheet: max 30ms)
    
    // Initialize sensor
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        Serial.print("wakeUp error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }
    
    error = sensor.stopPeriodicMeasurement();
    if (error != NO_ERROR) {
        Serial.print("stopPeriodicMeasurement error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }
    
    error = sensor.reinit();
    if (error != NO_ERROR) {
        Serial.print("reinit error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    }
    
    uint64_t serialNumber = 0;
    error = sensor.getSerialNumber(serialNumber);
    if (error != NO_ERROR) {
        Serial.print("getSerialNumber error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        Serial.println("SCD41 not found! Check wiring.");
        sendEvent(EVENT_CRITICAL, "SCD41 sensor not found at startup");
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);
        Serial.println("Sensor ready!");
        
        // Log successful startup
        char startupMsg[64];
        snprintf(startupMsg, sizeof(startupMsg), "Sensor started, serial: %08X%08X", 
                 (uint32_t)(serialNumber >> 32), (uint32_t)(serialNumber & 0xFFFFFFFF));
        sendEvent(EVENT_INFO, startupMsg);
    }
    
    // Power down sensor until first measurement
    sensor.powerDown();

    // Initialize forced recalibration module
    frcInit();

    Serial.println();
}

void loop() {
    // Check for forced recalibration button press
    if (frcCheckButton(sensor, [](int type, const char* msg) {
        return sendEvent((EventType)type, msg);
    })) {
        return;  // FRC ran, skip this loop iteration
    }

    uint16_t co2 = 0;
    float temp = 0.0;
    float humidity = 0.0;
    
    // Wake sensor from sleep (datasheet: 30ms max)
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("wakeUp error: ");
        Serial.println(errorMessage);
        
        totalI2CErrors++;
        consecutiveI2CFailures++;
        consecutiveFailures++;
        
        // Send error to server
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "I2C wakeUp failed: %s (consecutive: %lu)", 
                 errorMessage, consecutiveI2CFailures);
        sendEvent(EVENT_ERROR, errMsg);
        
        // Attempt recovery after multiple failures
        if (consecutiveI2CFailures >= 3) {
            sendEvent(EVENT_WARNING, "Attempting I2C bus recovery");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed - check wiring");
            }
        }
        
        delay(MEASUREMENT_INTERVAL_MS);
        return;
    }
    
    // Single shot measurement - no need to discard first reading
    // (Datasheet v1.6 removed this recommendation)
    // Takes ~5 seconds to complete
    error = sensor.measureAndReadSingleShot(co2, temp, humidity);
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("measureAndReadSingleShot error: ");
        Serial.println(errorMessage);
        
        totalI2CErrors++;
        consecutiveI2CFailures++;
        consecutiveFailures++;
        
        // Send error to server
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "I2C measurement failed: %s (consecutive: %lu)", 
                 errorMessage, consecutiveI2CFailures);
        sendEvent(EVENT_ERROR, errMsg);
        
        // Attempt recovery after multiple failures
        if (consecutiveI2CFailures >= 3) {
            sendEvent(EVENT_WARNING, "Attempting I2C bus recovery after measurement failure");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed - sensor may be disconnected");
            }
        }
        
        sensor.powerDown();  // Still try to power down
        delay(MEASUREMENT_INTERVAL_MS);
        return;
    }
    
    // Reset I2C failure counter on successful measurement
    if (consecutiveI2CFailures > 0) {
        char recoverMsg[64];
        snprintf(recoverMsg, sizeof(recoverMsg), "I2C recovered after %lu consecutive failures", 
                 consecutiveI2CFailures);
        sendEvent(EVENT_INFO, recoverMsg);
        consecutiveI2CFailures = 0;
    }
    
    // Power down sensor immediately after reading to save power
    // (Datasheet Section 3.11 recommends this for single-shot mode)
    error = sensor.powerDown();
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("powerDown error: ");
        Serial.println(errorMessage);
        // Non-fatal, continue anyway
    }
    
    totalMeasurements++;
    
    // Sanity check readings
    if (co2 < 300 || co2 > 10000) {
        char warnMsg[64];
        snprintf(warnMsg, sizeof(warnMsg), "Unusual CO2 reading: %d ppm", co2);
        sendEvent(EVENT_WARNING, warnMsg);
    }
    
    // Print locally
    Serial.println("=== Reading ===");
    Serial.printf("CO2: %d ppm | Temp: %.1f C | Humidity: %.1f %%\n", co2, temp, humidity);
    
    // Send to server
    if (sendData(co2, temp, humidity)) {
        successfulUploads++;
        consecutiveFailures = 0;
    } else {
        consecutiveFailures++;
    }
    
    // Print diagnostics every 100 measurements
    if (totalMeasurements % 100 == 0) {
        printDiagnostics();
        
        // Send periodic health report
        char healthMsg[128];
        snprintf(healthMsg, sizeof(healthMsg), 
                 "Health: %lu measurements, %.1f%% success, %lu I2C errors", 
                 totalMeasurements, 
                 (100.0 * successfulUploads / totalMeasurements),
                 totalI2CErrors);
        sendEvent(EVENT_INFO, healthMsg);
    }
    
    // Alert if too many consecutive failures
    if (consecutiveFailures >= 10) {
        Serial.println("WARNING: 10+ consecutive failures!");
        sendEvent(EVENT_CRITICAL, "10+ consecutive upload failures");
        
        // Long flash pattern to indicate serious problem
        for (int i = 0; i < 5; i++) {
            flashLED(1, 500);
            delay(200);
        }
    }
    
    // Calculate remaining wait time
    // measureAndReadSingleShot takes ~5 seconds, so subtract that
    unsigned long waitTime = MEASUREMENT_INTERVAL_MS > 5000 ?
                             MEASUREMENT_INTERVAL_MS - 5000 : 0;

    Serial.printf("Waiting %lu ms (hold BOOT to calibrate)...\n\n", waitTime);

    // Non-blocking wait that checks for FRC button
    unsigned long waitStart = millis();
    while (millis() - waitStart < waitTime) {
        // Check for calibration button during wait
        if (frcCheckButton(sensor, [](int type, const char* msg) {
            return sendEvent((EventType)type, msg);
        })) {
            return;  // FRC ran, restart loop
        }
        delay(100);  // Check every 100ms
    }
}