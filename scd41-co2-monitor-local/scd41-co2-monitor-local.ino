/*
 * SCD41 CO2 Monitor - Local Version
 *
 * Simplified sensor-only version that sends data to a local Flask API.
 * No OLED display, no IR blaster, no HTTPS, no authentication.
 *
 * Hardware:
 *   - ESP32 Dev Module
 *   - Sensirion SCD41 CO2 sensor (I2C)
 *
 * Wiring:
 *   SCD41 (I2C):
 *   VDD  -> 3V3
 *   GND  -> GND
 *   SDA  -> GPIO 21
 *   SCL  -> GPIO 22
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "secrets.h"

// =========================
// CONFIG
// =========================
const unsigned long MEASUREMENT_INTERVAL_MS = 60000;   // 1 minute between readings
const unsigned long BATCH_INTERVAL_MS = 600000;        // 10 minutes between uploads
const int MAX_BUFFER_SIZE = 15;                        // Max readings to buffer
const unsigned long WIFI_RETRY_DELAY_MS = 500;
const int WIFI_MAX_ATTEMPTS = 30;

#define LED_PIN 2

// Sensor
SensirionI2cScd4x sensor;

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

static char errorMessage[64];
static int16_t error;

// Tracking stats
static uint32_t consecutiveFailures = 0;
static uint32_t consecutiveI2CFailures = 0;
static uint32_t totalMeasurements = 0;
static uint32_t successfulUploads = 0;
static uint32_t totalI2CErrors = 0;
static uint32_t totalWiFiReconnects = 0;

// Batch buffer
struct Reading {
    uint16_t co2;
    float temp;
    float humidity;
    char timestamp[25];
};

Reading readingBuffer[MAX_BUFFER_SIZE];
int bufferCount = 0;
unsigned long lastBatchSentMs = 0;

// =========================
// UTILITY FUNCTIONS
// =========================

void flashLED(int times, int duration = 100) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(duration);
        digitalWrite(LED_PIN, LOW);
        if (i < times - 1) delay(duration);
    }
}

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_ATTEMPTS) {
        delay(WIFI_RETRY_DELAY_MS);
        Serial.print(".");
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
        for (int i = 0; i < 10; i++) {
            flashLED(1, 50);
            delay(50);
        }
    }
}

void getISOTimestamp(char* buffer, size_t len) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        strftime(buffer, len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    } else {
        snprintf(buffer, len, "1970-01-01T00:00:%02luZ", millis() / 1000 % 60);
    }
}

bool sendEvent(const char* eventType, const char* message) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[EVENT NOT SENT - NO WIFI] %s\n", message);
        return false;
    }

    HTTPClient http;
    String url = String(apiEndpoint) + "/api/sensor/log";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    String payload = "{\"device\":\"" + String(deviceName) +
                     "\",\"event_type\":\"" + String(eventType) +
                     "\",\"message\":\"" + String(message) +
                     "\",\"uptime\":" + String(millis() / 1000) + "}";

    Serial.print("[EVENT] Sending: ");
    Serial.println(payload);

    int httpCode = http.POST(payload);
    http.end();

    return httpCode == 200;
}

bool sendBatch() {
    if (bufferCount == 0) return true;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, attempting reconnect for batch...");
        totalWiFiReconnects++;
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Still no WiFi, batch deferred.");
            return false;
        }
        sendEvent("warning", "WiFi reconnected for batch send");
    }

    String payload = "{\"device\":\"" + String(deviceName) + "\",\"readings\":[";
    for (int i = 0; i < bufferCount; i++) {
        if (i > 0) payload += ",";
        payload += "{\"co2\":" + String(readingBuffer[i].co2) +
                   ",\"temp\":" + String(readingBuffer[i].temp, 1) +
                   ",\"humidity\":" + String(readingBuffer[i].humidity, 1) +
                   ",\"ts\":\"" + String(readingBuffer[i].timestamp) + "\"}";
    }
    payload += "]}";

    Serial.print("Sending batch: ");
    Serial.println(payload);

    HTTPClient http;
    String url = String(apiEndpoint) + "/api/sensor/batch";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);

    int httpCode = http.POST(payload);
    bool success = false;

    if (httpCode == 200) {
        Serial.printf("Batch sent OK (%d readings)\n", bufferCount);
        bufferCount = 0;
        success = true;
    } else {
        Serial.printf("Batch failed: %d\n", httpCode);
    }

    http.end();
    return success;
}

bool recoverI2C() {
    Serial.println("Attempting I2C bus recovery...");

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

    // Reinitialize I2C and sensor
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

void printDiagnostics() {
    Serial.println("\n=== DIAGNOSTICS ===");
    Serial.printf("Device: %s\n", deviceName);
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Total measurements: %lu\n", totalMeasurements);
    Serial.printf("Successful uploads: %lu\n", successfulUploads);
    Serial.printf("Success rate: %.1f%%\n", totalMeasurements > 0 ?
                  (100.0 * successfulUploads / totalMeasurements) : 0);
    Serial.printf("I2C errors: %lu\n", totalI2CErrors);
    Serial.printf("WiFi reconnects: %lu\n", totalWiFiReconnects);
    Serial.printf("Buffer: %d/%d\n", bufferCount, MAX_BUFFER_SIZE);
    Serial.println("===================\n");
}

// =========================
// SETUP
// =========================
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(LED_PIN, OUTPUT);

    Serial.println("\n================================");
    Serial.printf("SCD41 CO2 Monitor - Local (%s)\n", deviceName);
    Serial.println("================================");
    Serial.printf("API Endpoint: %s\n", apiEndpoint);

    // Connect WiFi
    connectWiFi();

    // Sync time
    if (WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        Serial.println("Waiting for NTP time sync...");
        struct tm timeinfo;
        int attempts = 0;
        while (!getLocalTime(&timeinfo) && attempts < 10) {
            delay(500);
            attempts++;
        }
        if (attempts < 10) {
            char timeStr[25];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            Serial.printf("Time synchronized: %s\n", timeStr);
        }
    }

    // Initialize sensor
    Serial.println("Initializing SCD41 sensor...");
    Wire.begin(21, 22);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);

    sensor.wakeUp();
    delay(30);
    sensor.stopPeriodicMeasurement();
    delay(500);
    sensor.reinit();

    uint64_t serialNumber = 0;
    error = sensor.getSerialNumber(serialNumber);
    if (error != NO_ERROR) {
        Serial.println("SCD41 not found! Check wiring.");
        sendEvent("critical", "SCD41 sensor not found at startup");
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);

        char startupMsg[64];
        snprintf(startupMsg, sizeof(startupMsg), "Sensor started, serial: %08X%08X",
                 (uint32_t)(serialNumber >> 32), (uint32_t)(serialNumber & 0xFFFFFFFF));
        sendEvent("info", startupMsg);
    }

    sensor.powerDown();
    Serial.println("Setup complete!\n");
}

// =========================
// MAIN LOOP
// =========================
void loop() {
    uint16_t co2 = 0;
    float temp = 0.0;
    float humidity = 0.0;

    // Wake sensor
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("wakeUp error: ");
        Serial.println(errorMessage);

        totalI2CErrors++;
        consecutiveI2CFailures++;
        consecutiveFailures++;

        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "I2C wakeUp failed: %s (consecutive: %lu)",
                 errorMessage, consecutiveI2CFailures);
        sendEvent("error", errMsg);

        if (consecutiveI2CFailures >= 3) {
            sendEvent("warning", "Attempting I2C bus recovery");
            if (recoverI2C()) {
                sendEvent("info", "I2C recovery successful");
                consecutiveI2CFailures = 0;
            } else {
                sendEvent("critical", "I2C recovery failed - check wiring");
            }
        }

        delay(MEASUREMENT_INTERVAL_MS);
        return;
    }

    // Measure
    error = sensor.measureAndReadSingleShot(co2, temp, humidity);
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("measureAndReadSingleShot error: ");
        Serial.println(errorMessage);

        totalI2CErrors++;
        consecutiveI2CFailures++;
        consecutiveFailures++;

        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "I2C measurement failed: %s (consecutive: %lu)",
                 errorMessage, consecutiveI2CFailures);
        sendEvent("error", errMsg);

        if (consecutiveI2CFailures >= 3) {
            sendEvent("warning", "Attempting I2C bus recovery after measurement failure");
            if (recoverI2C()) {
                sendEvent("info", "I2C recovery successful");
                consecutiveI2CFailures = 0;
            } else {
                sendEvent("critical", "I2C recovery failed - sensor may be disconnected");
            }
        }

        sensor.powerDown();
        delay(MEASUREMENT_INTERVAL_MS);
        return;
    }

    // Success - reset counters
    if (consecutiveI2CFailures > 0) {
        char recoverMsg[64];
        snprintf(recoverMsg, sizeof(recoverMsg), "I2C recovered after %lu consecutive failures",
                 consecutiveI2CFailures);
        sendEvent("info", recoverMsg);
        consecutiveI2CFailures = 0;
    }

    sensor.powerDown();
    totalMeasurements++;

    // Sanity check
    if (co2 < 300 || co2 > 10000) {
        char warnMsg[64];
        snprintf(warnMsg, sizeof(warnMsg), "Unusual CO2 reading: %d ppm", co2);
        sendEvent("warning", warnMsg);
    }

    // Print locally
    Serial.println("=== Reading ===");
    Serial.printf("CO2: %d ppm | Temp: %.1f C | Humidity: %.1f %%\n", co2, temp, humidity);

    // Buffer reading
    if (bufferCount < MAX_BUFFER_SIZE) {
        readingBuffer[bufferCount].co2 = co2;
        readingBuffer[bufferCount].temp = temp;
        readingBuffer[bufferCount].humidity = humidity;
        getISOTimestamp(readingBuffer[bufferCount].timestamp, 25);
        bufferCount++;
        Serial.printf("Buffered reading %d/%d\n", bufferCount, MAX_BUFFER_SIZE);
    } else {
        Serial.println("Buffer full, dropping reading");
    }

    // Check if time to send batch
    unsigned long now = millis();
    bool shouldSend = (now - lastBatchSentMs >= BATCH_INTERVAL_MS) ||
                      (bufferCount >= MAX_BUFFER_SIZE - 2);

    if (shouldSend && bufferCount > 0) {
        int sentCount = bufferCount;
        if (sendBatch()) {
            successfulUploads += sentCount;
            lastBatchSentMs = now;
            consecutiveFailures = 0;
            flashLED(2);
            Serial.printf("Upload OK: %d readings\n", sentCount);
        } else {
            consecutiveFailures++;
            flashLED(3);
            Serial.println("Upload failed");
        }
    }

    // Diagnostics every 100 measurements
    if (totalMeasurements % 100 == 0) {
        printDiagnostics();
        char healthMsg[128];
        snprintf(healthMsg, sizeof(healthMsg),
                 "Health: %lu measurements, %.1f%% success, %lu I2C errors",
                 totalMeasurements,
                 (100.0 * successfulUploads / totalMeasurements),
                 totalI2CErrors);
        sendEvent("info", healthMsg);
    }

    // Alert on consecutive failures
    if (consecutiveFailures >= 10) {
        Serial.println("WARNING: 10+ consecutive failures!");
        sendEvent("critical", "10+ consecutive upload failures");
        for (int i = 0; i < 5; i++) {
            flashLED(1, 500);
            delay(200);
        }
    }

    // Wait for next measurement
    unsigned long waitTime = MEASUREMENT_INTERVAL_MS > 5000 ?
                             MEASUREMENT_INTERVAL_MS - 5000 : 0;

    Serial.printf("Waiting %lu ms...\n\n", waitTime);
    delay(waitTime);
}
