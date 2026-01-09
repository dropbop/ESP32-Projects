/*
 * SCD41 CO2 Monitor with OLED Display
 * 
 * Hardware:
 *   - ESP32 Dev Module
 *   - Sensirion SCD41 CO2 sensor (I2C)
 *   - Inland 1.3" 128x64 OLED (SH1106, SPI)
 * 
 * Wiring:
 *   SCD41 (I2C):           OLED (SPI):
 *   VDD  -> 3V3            VCC  -> VIN (5V)
 *   GND  -> GND            GND  -> GND
 *   SDA  -> GPIO 21        CLK  -> GPIO 25
 *   SCL  -> GPIO 22        MOSI -> GPIO 26
 *                          RES  -> GPIO 12
 *                          DC   -> GPIO 14
 *                          CS   -> GPIO 27
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "secrets.h"
#include "forced_calibration.h"

// =========================
// OLED PIN DEFINITIONS
// =========================
#define OLED_CLK   25
#define OLED_MOSI  26
#define OLED_CS    27
#define OLED_DC    14
#define OLED_RES   12

// OLED display (Software SPI)
U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, OLED_CLK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RES);

// =========================
// CONFIG
// =========================
const char* endpoint = "https://www.dropbop.xyz/api/sensor";
const char* eventEndpoint = "https://www.dropbop.xyz/api/sensor/log";
const char* deviceName = "office";

const unsigned long MEASUREMENT_INTERVAL_MS = 60000;
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

// Tracking
static uint32_t consecutiveFailures = 0;
static uint32_t consecutiveI2CFailures = 0;
static uint32_t totalMeasurements = 0;
static uint32_t successfulUploads = 0;
static uint32_t totalI2CErrors = 0;
static uint32_t totalWiFiReconnects = 0;

// Batch config
const unsigned long BATCH_INTERVAL_MS = 600000;
const int MAX_BUFFER_SIZE = 15;

struct Reading {
    uint16_t co2;
    float temp;
    float humidity;
    char timestamp[25];
};

Reading readingBuffer[MAX_BUFFER_SIZE];
int bufferCount = 0;
unsigned long lastBatchSentMs = 0;

// Current display values
uint16_t displayCO2 = 0;
float displayTemp = 0.0;
float displayHumidity = 0.0;
bool displayError = false;
String displayStatus = "Starting...";

enum EventType {
    EVENT_INFO,
    EVENT_WARNING,
    EVENT_ERROR,
    EVENT_CRITICAL
};

// =========================
// DISPLAY FUNCTIONS
// =========================

void updateDisplay() {
    u8g2.clearBuffer();
    
    // CO2 reading - big and centered
    u8g2.setFont(u8g2_font_logisoso28_tn);  // Large numeric font
    char co2Str[8];
    if (displayError) {
        strcpy(co2Str, "ERR");
        u8g2.setFont(u8g2_font_ncenB14_tr);
    } else if (displayCO2 == 0) {
        strcpy(co2Str, "---");
    } else {
        snprintf(co2Str, sizeof(co2Str), "%d", displayCO2);
    }
    
    // Center the CO2 value
    int width = u8g2.getStrWidth(co2Str);
    u8g2.drawStr((128 - width) / 2 - 15, 32, co2Str);
    
    // "ppm" label
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(90, 32, "ppm");
    
    // Temp and humidity on same line
    u8g2.setFont(u8g2_font_6x10_tr);
    char envStr[32];
    if (displayCO2 > 0) {
        snprintf(envStr, sizeof(envStr), "%.1fC  %.0f%%", displayTemp, displayHumidity);
    } else {
        strcpy(envStr, "--.-C  --%");
    }
    width = u8g2.getStrWidth(envStr);
    u8g2.drawStr((128 - width) / 2, 45, envStr);
    
    // Divider line
    u8g2.drawHLine(0, 50, 128);
    
    // Status bar at bottom
    u8g2.setFont(u8g2_font_5x7_tr);
    
    // WiFi indicator
    if (WiFi.status() == WL_CONNECTED) {
        u8g2.drawStr(0, 62, "WiFi OK");
    } else {
        u8g2.drawStr(0, 62, "No WiFi");
    }
    
    // Buffer status
    char bufStr[12];
    snprintf(bufStr, sizeof(bufStr), "Buf:%d/%d", bufferCount, MAX_BUFFER_SIZE);
    u8g2.drawStr(45, 62, bufStr);
    
    // Uptime (minutes)
    char uptimeStr[10];
    unsigned long mins = millis() / 60000;
    if (mins < 60) {
        snprintf(uptimeStr, sizeof(uptimeStr), "%lum", mins);
    } else {
        snprintf(uptimeStr, sizeof(uptimeStr), "%luh%lum", mins / 60, mins % 60);
    }
    u8g2.drawStr(100, 62, uptimeStr);
    
    u8g2.sendBuffer();
}

void displayMessage(const char* line1, const char* line2 = nullptr) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    
    int y = line2 ? 25 : 35;
    int w = u8g2.getStrWidth(line1);
    u8g2.drawStr((128 - w) / 2, y, line1);
    
    if (line2) {
        w = u8g2.getStrWidth(line2);
        u8g2.drawStr((128 - w) / 2, 45, line2);
    }
    
    u8g2.sendBuffer();
}

void displayConnecting(int attempt, int maxAttempts) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(20, 25, "Connecting...");
    
    // Progress bar
    u8g2.drawFrame(14, 35, 100, 12);
    int progress = (attempt * 100) / maxAttempts;
    u8g2.drawBox(15, 36, progress, 10);
    
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[20];
    snprintf(buf, sizeof(buf), "Attempt %d/%d", attempt, maxAttempts);
    int w = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - w) / 2, 58, buf);
    
    u8g2.sendBuffer();
}

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
    http.setTimeout(5000);
    
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
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        displayConnecting(attempts + 1, WIFI_MAX_ATTEMPTS);
        attempts++;
    }
    digitalWrite(LED_PIN, LOW);
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        displayMessage("WiFi Connected!", WiFi.localIP().toString().c_str());
        delay(1000);
    } else {
        Serial.println();
        Serial.println("WiFi connection failed!");
        displayMessage("WiFi Failed!", "Continuing offline");
        delay(2000);
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
        sendEvent(EVENT_WARNING, "WiFi reconnected for batch send");
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
    http.begin("https://www.dropbop.xyz/api/sensor/batch");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Sensor-Token", sensorToken);
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
    
    // Initialize OLED first for visual feedback
    Serial.println("Initializing OLED...");
    u8g2.begin();
    displayMessage("CO2 Monitor", "Starting...");
    delay(500);
    
    Serial.println("\n================================");
    Serial.println("SCD41 CO2 Monitor + OLED");
    Serial.println("================================");
    
    // Connect WiFi
    displayMessage("Connecting WiFi...");
    connectWiFi();
    
    // Sync time
    if (WiFi.status() == WL_CONNECTED) {
        displayMessage("Syncing time...");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        Serial.println("Waiting for NTP time sync...");
        struct tm timeinfo;
        int attempts = 0;
        while (!getLocalTime(&timeinfo) && attempts < 10) {
            delay(500);
            attempts++;
        }
        if (attempts < 10) {
            Serial.println("Time synchronized!");
            char timeStr[20];
            strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
            displayMessage("Time synced", timeStr);
            delay(1000);
        }
    }
    
    // Initialize sensor
    displayMessage("Init sensor...");
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
        displayMessage("Sensor Error!", "Check wiring");
        sendEvent(EVENT_CRITICAL, "SCD41 sensor not found at startup");
        displayError = true;
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);
        
        displayMessage("Sensor ready!");
        delay(1000);
        
        char startupMsg[64];
        snprintf(startupMsg, sizeof(startupMsg), "Sensor started, serial: %08X%08X", 
                 (uint32_t)(serialNumber >> 32), (uint32_t)(serialNumber & 0xFFFFFFFF));
        sendEvent(EVENT_INFO, startupMsg);
    }
    
    sensor.powerDown();
    frcInit();
    
    // Show initial display
    displayMessage("Waiting for", "first reading...");
    Serial.println("Setup complete!\n");
}

// =========================
// MAIN LOOP
// =========================
void loop() {
    // Check for forced recalibration
    if (frcCheckButton(sensor, [](int type, const char* msg) {
        return sendEvent((EventType)type, msg);
    })) {
        displayMessage("Calibration", "Complete!");
        delay(2000);
        return;
    }

    uint16_t co2 = 0;
    float temp = 0.0;
    float humidity = 0.0;
    
    // Show "measuring" on display
    displayStatus = "Measuring...";
    
    // Wake sensor
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("wakeUp error: ");
        Serial.println(errorMessage);
        
        totalI2CErrors++;
        consecutiveI2CFailures++;
        consecutiveFailures++;
        displayError = true;
        updateDisplay();
        
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "I2C wakeUp failed: %s (consecutive: %lu)", 
                 errorMessage, consecutiveI2CFailures);
        sendEvent(EVENT_ERROR, errMsg);
        
        if (consecutiveI2CFailures >= 3) {
            displayMessage("I2C Error", "Recovering...");
            sendEvent(EVENT_WARNING, "Attempting I2C bus recovery");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
                displayMessage("I2C Recovered!");
                delay(1000);
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed - check wiring");
                displayMessage("I2C Failed!", "Check wiring");
                delay(2000);
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
        displayError = true;
        updateDisplay();
        
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "I2C measurement failed: %s (consecutive: %lu)", 
                 errorMessage, consecutiveI2CFailures);
        sendEvent(EVENT_ERROR, errMsg);
        
        if (consecutiveI2CFailures >= 3) {
            displayMessage("Sensor Error", "Recovering...");
            sendEvent(EVENT_WARNING, "Attempting I2C bus recovery after measurement failure");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed - sensor may be disconnected");
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
        sendEvent(EVENT_INFO, recoverMsg);
        consecutiveI2CFailures = 0;
    }
    displayError = false;
    
    sensor.powerDown();
    totalMeasurements++;
    
    // Update display values
    displayCO2 = co2;
    displayTemp = temp;
    displayHumidity = humidity;
    updateDisplay();
    
    // Sanity check
    if (co2 < 300 || co2 > 10000) {
        char warnMsg[64];
        snprintf(warnMsg, sizeof(warnMsg), "Unusual CO2 reading: %d ppm", co2);
        sendEvent(EVENT_WARNING, warnMsg);
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

    // Update display with new buffer count
    updateDisplay();

    // Check if time to send batch
    unsigned long now = millis();
    bool shouldSend = (now - lastBatchSentMs >= BATCH_INTERVAL_MS) ||
                      (bufferCount >= MAX_BUFFER_SIZE - 2);

    if (shouldSend && bufferCount > 0) {
        displayMessage("Uploading...");
        int sentCount = bufferCount;
        if (sendBatch()) {
            successfulUploads += sentCount;
            lastBatchSentMs = now;
            consecutiveFailures = 0;
            flashLED(2);
            displayMessage("Upload OK!", String(String(sentCount) + " readings").c_str());
            delay(1000);
        } else {
            consecutiveFailures++;
            flashLED(3);
            displayMessage("Upload Failed");
            delay(1000);
        }
        updateDisplay();
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
        sendEvent(EVENT_INFO, healthMsg);
    }
    
    // Alert on consecutive failures
    if (consecutiveFailures >= 10) {
        Serial.println("WARNING: 10+ consecutive failures!");
        displayMessage("WARNING!", "10+ failures");
        sendEvent(EVENT_CRITICAL, "10+ consecutive upload failures");
        for (int i = 0; i < 5; i++) {
            flashLED(1, 500);
            delay(200);
        }
    }
    
    // Wait for next measurement
    unsigned long waitTime = MEASUREMENT_INTERVAL_MS > 5000 ?
                             MEASUREMENT_INTERVAL_MS - 5000 : 0;

    Serial.printf("Waiting %lu ms (hold BOOT to calibrate)...\n\n", waitTime);

    unsigned long waitStart = millis();
    while (millis() - waitStart < waitTime) {
        if (frcCheckButton(sensor, [](int type, const char* msg) {
            return sendEvent((EventType)type, msg);
        })) {
            displayMessage("Calibration", "Complete!");
            delay(2000);
            return;
        }
        delay(100);
    }
}
