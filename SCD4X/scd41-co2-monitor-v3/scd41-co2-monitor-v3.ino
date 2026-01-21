/*
 * SCD41 CO2 Monitor v3 - OLED + IR Blaster
 *
 * Combines SCD41 environmental monitoring, OLED display, and IR remote control.
 * Uses periodic measurement mode with 60-second readings sent to local Flask API.
 *
 * Hardware:
 *   - ESP32 Dev Module
 *   - Sensirion SCD41 CO2 sensor (I2C)
 *   - Inland 1.3" 128x64 OLED (SH1106, SPI) - mounted upside-down
 *   - IR LED on GPIO 4
 *
 * Wiring:
 *   SCD41 (I2C):           OLED (SPI):              IR LED:
 *   VDD  -> 3V3            VCC  -> VIN (5V)         Anode  -> 100ohm -> GPIO 4
 *   GND  -> GND            GND  -> GND              Cathode -> GND
 *   SDA  -> GPIO 21        CLK  -> GPIO 25
 *   SCL  -> GPIO 22        MOSI -> GPIO 26
 *                          RES  -> GPIO 12
 *                          DC   -> GPIO 14
 *                          CS   -> GPIO 27
 *
 * Serial Commands (for IR control):
 *   on      - Send AC ON signal once
 *   off     - Send AC OFF signal once
 *   spamon  - Start spamming ON signal (250ms interval)
 *   spamoff - Start spamming OFF signal (250ms interval)
 *   stop    - Stop spamming
 *   spam    - Toggle spam mode
 *   help    - Print available commands
 *
 * Calibration:
 *   Hold BOOT button for 3 seconds to start forced recalibration.
 *   Take sensor outside to fresh air first!
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_task_wdt.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

#include "secrets.h"

// Event types (must be defined before forced_calibration.h)
enum EventType {
    EVENT_INFO = 0,
    EVENT_WARNING = 1,
    EVENT_ERROR = 2,
    EVENT_CRITICAL = 3
};

// Forward declaration for forced_calibration.h callback
bool sendEvent(EventType type, const char* message);

#include "forced_calibration.h"

// ===========================================
// Configuration
// ===========================================

// Measurement interval (sensor updates internally every 5s, we read every 60s)
const unsigned long MEASUREMENT_INTERVAL_MS = 60000;

// Altitude compensation - Houston, TX is ~15m above sea level
const uint16_t SENSOR_ALTITUDE_METERS = 15;

// Temperature offset compensation for self-heating
const float TEMPERATURE_OFFSET_C = 3.6;

// WiFi connection
const unsigned long WIFI_RETRY_DELAY_MS = 500;
const int WIFI_MAX_ATTEMPTS = 30;

// Watchdog timeout - resets ESP32 if loop hangs
const int WATCHDOG_TIMEOUT_SECONDS = 120;

// IR spam interval
const unsigned long IR_SPAM_INTERVAL_MS = 250;

// Display update interval (update more frequently than measurements for responsiveness)
const unsigned long DISPLAY_UPDATE_INTERVAL_MS = 1000;

// ===========================================
// Hardware Pin Definitions
// ===========================================

#define LED_PIN 2

// I2C for SCD41
#define I2C_SDA 21
#define I2C_SCL 22

// SPI for OLED
#define OLED_CLK  25
#define OLED_MOSI 26
#define OLED_CS   27
#define OLED_DC   14
#define OLED_RES  12

// IR LED
#define IR_LED_PIN 4

// ===========================================
// OLED Display (Software SPI, rotated 180° for upside-down mounting)
// ===========================================

U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R2, OLED_CLK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RES);

// ===========================================
// IR Signal Data (Whynter AC - from Flipper Zero capture)
// 38kHz carrier, timings in microseconds
// ===========================================

// AC_ON raw timing data (3 bursts)
const uint16_t AC_ON_RAW[] = {
    4384, 4450, 512, 1639, 516, 591, 486, 1641, 514, 1642, 512, 563, 514, 563, 514, 1640,
    514, 563, 514, 562, 515, 1639, 515, 564, 513, 563, 514, 1641, 514, 1641, 514, 563,
    514, 1640, 515, 563, 514, 562, 515, 562, 515, 1642, 513, 1641, 514, 1639, 516, 1638,
    516, 1640, 515, 1638, 518, 1639, 515, 1643, 565, 509, 515, 562, 515, 562, 515, 591,
    486, 562, 515, 562, 515, 562, 515, 562, 515, 1641, 514, 562, 515, 1641, 514, 563,
    514, 562, 515, 1668, 487, 1641, 514, 1641, 514, 562, 515, 1641, 567, 510, 514, 1641,
    514, 1642, 513, 5239,
    // Second burst (repeat)
    4385, 4451, 512, 1640, 515, 563, 514, 1642, 565, 1588, 514, 562, 515, 562, 515, 1640,
    515, 562, 515, 562, 515, 1640, 515, 562, 515, 562, 515, 1640, 514, 1641, 567, 509,
    515, 1638, 517, 562, 515, 563, 514, 562, 515, 1639, 516, 1640, 515, 1640, 515, 1639,
    516, 1639, 516, 1641, 514, 1640, 515, 1640, 515, 562, 515, 562, 515, 563, 514, 562,
    515, 562, 515, 562, 515, 563, 514, 563, 514, 1641, 514, 562, 515, 1638, 516, 562,
    515, 563, 514, 1640, 515, 1640, 514, 1641, 514, 591, 486, 1639, 516, 563, 567, 1588,
    514, 1668, 487, 5240,
    // Third burst
    4384, 4447, 516, 1668, 486, 1641, 514, 562, 515, 1669, 486, 563, 514, 1641, 514, 564,
    513, 1639, 516, 562, 515, 1641, 514, 1668, 487, 562, 515, 563, 514, 1640, 515, 562,
    515, 1639, 516, 563, 514, 563, 514, 1640, 515, 562, 515, 563, 568, 508, 516, 562,
    515, 562, 515, 562, 515, 562, 515, 562, 515, 562, 515, 562, 515, 562, 515, 562,
    515, 1639, 516, 562, 515, 562, 515, 562, 515, 562, 515, 562, 515, 563, 514, 563,
    514, 563, 514, 591, 486, 1639, 516, 562, 515, 1668, 487, 1640, 515, 562, 515, 1640,
    515, 1641, 514
};
const uint16_t AC_ON_RAW_LEN = sizeof(AC_ON_RAW) / sizeof(AC_ON_RAW[0]);

// AC_OFF raw timing data (2 bursts)
const uint16_t AC_OFF_RAW[] = {
    4379, 4477, 486, 1643, 512, 564, 513, 1669, 486, 1669, 486, 564, 514, 565, 512, 1639,
    569, 511, 513, 563, 514, 1641, 567, 510, 515, 565, 512, 1669, 539, 1588, 514, 591,
    486, 1640, 515, 562, 515, 1669, 486, 1641, 514, 1642, 513, 1643, 512, 564, 513, 1642,
    513, 1641, 514, 1642, 512, 564, 513, 563, 514, 564, 513, 591, 486, 1669, 486, 563,
    514, 563, 514, 1669, 486, 1641, 514, 1642, 513, 591, 486, 564, 514, 564, 513, 591,
    486, 564, 513, 591, 486, 564, 513, 563, 514, 1642, 513, 1642, 513, 1641, 514, 1641,
    514, 1640, 515, 5241,
    // Second burst
    4385, 4449, 514, 1641, 514, 563, 514, 1640, 515, 1641, 514, 563, 514, 563, 514, 1644,
    511, 563, 514, 563, 514, 1641, 514, 562, 515, 563, 514, 1641, 514, 1640, 515, 563,
    514, 1641, 514, 564, 513, 1643, 512, 1643, 512, 1642, 513, 1640, 515, 563, 514, 1642,
    514, 1639, 516, 1642, 513, 563, 514, 563, 514, 563, 514, 565, 512, 1642, 513, 563,
    567, 509, 516, 1641, 514, 1668, 486, 1642, 513, 563, 514, 563, 514, 591, 486, 565,
    512, 562, 516, 562, 515, 562, 515, 563, 514, 1641, 514, 1641, 514, 1640, 514, 1641,
    514, 1642, 513
};
const uint16_t AC_OFF_RAW_LEN = sizeof(AC_OFF_RAW) / sizeof(AC_OFF_RAW[0]);

// ===========================================
// Global state
// ===========================================

SensirionI2cScd4x sensor;
IRsend irsend(IR_LED_PIN);

// Display state
static uint16_t displayCO2 = 0;
static float displayTemp = 0.0;
static float displayHumidity = 0.0;
static bool displayError = false;
static bool displayWaiting = true;
static unsigned long lastDisplayUpdate = 0;

// IR state
static bool irSpamming = false;
static bool irSpamOn = true;  // true = spam ON signal, false = spam OFF signal
static unsigned long lastIrSpam = 0;

// Stats
static uint32_t totalMeasurements = 0;
static uint32_t successfulUploads = 0;
static uint32_t totalI2CErrors = 0;
static uint32_t totalWiFiReconnects = 0;
static uint32_t consecutiveI2CFailures = 0;
static uint32_t consecutiveUploadFailures = 0;

// Timing
static unsigned long lastMeasurementTime = 0;

// ===========================================
// Display Functions
// ===========================================

void updateDisplay() {
    u8g2.clearBuffer();

    // CO2 reading - big and centered
    u8g2.setFont(u8g2_font_logisoso28_tn);  // Large numeric font
    char co2Str[8];

    if (displayError) {
        strcpy(co2Str, "ERR");
        u8g2.setFont(u8g2_font_ncenB14_tr);
    } else if (displayWaiting || displayCO2 == 0) {
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
    if (!displayWaiting && displayCO2 > 0) {
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
        int rssi = WiFi.RSSI();
        char wifiStr[12];
        snprintf(wifiStr, sizeof(wifiStr), "WiFi %d", rssi);
        u8g2.drawStr(0, 62, wifiStr);
    } else {
        u8g2.drawStr(0, 62, "No WiFi");
    }

    // IR status (if spamming)
    if (irSpamming) {
        u8g2.drawStr(50, 62, irSpamOn ? "IR:ON" : "IR:OFF");
    }

    // Uptime
    char uptimeStr[12];
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

void displayWaitingCountdown(unsigned long remainingMs) {
    u8g2.clearBuffer();

    // Title
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(22, 18, "Waiting for");
    u8g2.drawStr(18, 32, "first reading");

    // Progress bar
    unsigned long elapsed = MEASUREMENT_INTERVAL_MS - remainingMs;
    int progress = (elapsed * 100) / MEASUREMENT_INTERVAL_MS;
    u8g2.drawFrame(14, 42, 100, 8);
    u8g2.drawBox(15, 43, (progress * 98) / 100, 6);

    // Time remaining (M:SS format)
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[16];
    unsigned long secs = remainingMs / 1000;
    snprintf(buf, sizeof(buf), "%lu:%02lu left", secs / 60, secs % 60);
    int w = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - w) / 2, 58, buf);

    u8g2.sendBuffer();
}

// ===========================================
// LED feedback
// ===========================================

void flashLED(int times, int duration = 100) {
    for (int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(duration);
        digitalWrite(LED_PIN, LOW);
        if (i < times - 1) delay(duration);
    }
}

// ===========================================
// Event logging
// ===========================================

bool sendEvent(EventType type, const char* message) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("[Event not sent - no WiFi] ");
        Serial.println(message);
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
    String url = String(apiEndpoint) + "/api/sensor/log";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    String payload = "{\"device\":\"" + String(deviceName) +
                     "\",\"event_type\":\"" + String(typeStr) +
                     "\",\"message\":\"" + String(message) +
                     "\",\"uptime\":" + String(millis() / 1000) +
                     ",\"heap\":" + String(ESP.getFreeHeap()) +
                     ",\"total_measurements\":" + String(totalMeasurements) +
                     ",\"i2c_errors\":" + String(totalI2CErrors) + "}";

    int httpCode = http.POST(payload);
    http.end();

    return httpCode == 200;
}

// Wrapper for FRC module callback
bool frcEventCallback(int type, const char* msg) {
    return sendEvent((EventType)type, msg);
}

// FRC display callback - shows calibration progress on OLED
void frcDisplayUpdate(unsigned long remainingMs, unsigned long totalMs,
                      int readingCount, uint16_t currentCO2, float avgCO2) {
    u8g2.clearBuffer();

    // Title
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(8, 14, "CALIBRATING...");

    // Current CO2 reading
    u8g2.setFont(u8g2_font_6x10_tr);
    char buf[24];
    if (currentCO2 > 0) {
        snprintf(buf, sizeof(buf), "CO2: %d ppm", currentCO2);
    } else {
        strcpy(buf, "CO2: ---");
    }
    u8g2.drawStr(20, 28, buf);

    // Average
    if (readingCount > 0) {
        snprintf(buf, sizeof(buf), "Avg: %d ppm", (int)avgCO2);
    } else {
        strcpy(buf, "Avg: ---");
    }
    u8g2.drawStr(20, 40, buf);

    // Progress bar
    int progress = ((totalMs - remainingMs) * 100) / totalMs;
    u8g2.drawFrame(14, 46, 100, 8);
    u8g2.drawBox(15, 47, (progress * 98) / 100, 6);

    // Time remaining (MM:SS format)
    unsigned long secs = remainingMs / 1000;
    snprintf(buf, sizeof(buf), "%lu:%02lu left", secs / 60, secs % 60);
    int w = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - w) / 2, 62, buf);

    u8g2.sendBuffer();
}

// ===========================================
// WiFi
// ===========================================

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
        flashLED(10, 50);
    }
}

bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    Serial.println("WiFi disconnected, reconnecting...");
    totalWiFiReconnects++;
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        sendEvent(EVENT_WARNING, "WiFi reconnected after disconnect");
        return true;
    }
    return false;
}

// ===========================================
// Sensor data upload
// ===========================================

bool sendReading(uint16_t co2, float temp, float humidity) {
    if (!ensureWiFi()) {
        return false;
    }

    HTTPClient http;
    String url = String(apiEndpoint) + "/api/sensor";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    String payload = "{\"device\":\"" + String(deviceName) +
                     "\",\"co2\":" + String(co2) +
                     ",\"temp\":" + String(temp, 1) +
                     ",\"humidity\":" + String(humidity, 1) +
                     ",\"rssi\":" + String(WiFi.RSSI()) +
                     ",\"uptime\":" + String(millis() / 1000) +
                     ",\"heap\":" + String(ESP.getFreeHeap()) + "}";

    Serial.print("POST ");
    Serial.print(url);
    Serial.print(" -> ");

    int httpCode = http.POST(payload);
    http.end();

    if (httpCode == 200) {
        Serial.println("OK");
        return true;
    } else {
        Serial.print("Failed (");
        Serial.print(httpCode);
        Serial.println(")");
        return false;
    }
}

// ===========================================
// I2C recovery
// ===========================================

bool recoverI2C() {
    Serial.println("Attempting I2C recovery...");
    displayMessage("I2C Error", "Recovering...");

    // End I2C
    Wire.end();
    delay(100);

    // Manually clock out any stuck transaction
    pinMode(I2C_SDA, OUTPUT);
    pinMode(I2C_SCL, OUTPUT);

    // 9 clock pulses to release stuck slave
    digitalWrite(I2C_SDA, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
    }

    // Generate STOP condition
    digitalWrite(I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA, HIGH);

    delay(100);

    // Reinitialize
    Wire.begin(I2C_SDA, I2C_SCL);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(50);

    // Verify sensor responds
    uint64_t serialNumber = 0;
    int16_t error = sensor.getSerialNumber(serialNumber);

    if (error == 0) {
        Serial.println("I2C recovery successful");
        displayMessage("I2C Recovered!");
        delay(1000);

        // Restart periodic measurement
        sensor.stopPeriodicMeasurement();
        delay(500);
        sensor.startPeriodicMeasurement();

        return true;
    }

    Serial.println("I2C recovery failed");
    displayMessage("I2C Failed!", "Check wiring");
    delay(2000);
    return false;
}

// ===========================================
// IR Control
// ===========================================

void sendIROn() {
    irsend.sendRaw(AC_ON_RAW, AC_ON_RAW_LEN, 38);
}

void sendIROff() {
    irsend.sendRaw(AC_OFF_RAW, AC_OFF_RAW_LEN, 38);
}

void printHelp() {
    Serial.println();
    Serial.println("=== Serial Commands ===");
    Serial.println("  on      - Send AC ON signal once");
    Serial.println("  off     - Send AC OFF signal once");
    Serial.println("  spam    - Toggle continuous sending (uses current on/off state)");
    Serial.println("  spamon  - Start spamming ON signal");
    Serial.println("  spamoff - Start spamming OFF signal");
    Serial.println("  stop    - Stop spamming");
    Serial.println("  help    - Print this message");
    Serial.println("=======================");
    Serial.println();
}

void handleSerialCommands() {
    if (!Serial.available()) {
        return;
    }

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.length() == 0) {
        return;
    }

    if (cmd == "on") {
        sendIROn();
        Serial.println("[IR] AC ON signal sent");
    } else if (cmd == "off") {
        sendIROff();
        Serial.println("[IR] AC OFF signal sent");
    } else if (cmd == "spam") {
        irSpamming = !irSpamming;
        Serial.print("[IR] Spam mode: ");
        Serial.print(irSpamming ? "ON" : "OFF");
        if (irSpamming) {
            Serial.print(" (sending ");
            Serial.print(irSpamOn ? "ON" : "OFF");
            Serial.print(" signal)");
            lastIrSpam = millis();
        }
        Serial.println();
    } else if (cmd == "spamon") {
        irSpamming = true;
        irSpamOn = true;
        lastIrSpam = millis();
        Serial.println("[IR] Spamming AC ON signal");
    } else if (cmd == "spamoff") {
        irSpamming = true;
        irSpamOn = false;
        lastIrSpam = millis();
        Serial.println("[IR] Spamming AC OFF signal");
    } else if (cmd == "stop") {
        irSpamming = false;
        Serial.println("[IR] Spam stopped");
    } else if (cmd == "help") {
        printHelp();
    } else {
        Serial.print("[?] Unknown command: ");
        Serial.println(cmd);
        Serial.println("    Type 'help' for available commands");
    }
}

// ===========================================
// Diagnostics
// ===========================================

void printDiagnostics() {
    Serial.println();
    Serial.println("=== Diagnostics ===");
    Serial.print("Measurements: ");
    Serial.println(totalMeasurements);
    Serial.print("Uploads: ");
    Serial.print(successfulUploads);
    Serial.print(" (");
    if (totalMeasurements > 0) {
        Serial.print(100.0 * successfulUploads / totalMeasurements, 1);
    } else {
        Serial.print("0.0");
    }
    Serial.println("%)");
    Serial.print("I2C errors: ");
    Serial.println(totalI2CErrors);
    Serial.print("WiFi reconnects: ");
    Serial.println(totalWiFiReconnects);
    Serial.print("Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    Serial.print("Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    Serial.print("IR spam: ");
    if (irSpamming) {
        Serial.print("ACTIVE (");
        Serial.print(irSpamOn ? "ON" : "OFF");
        Serial.println(" signal)");
    } else {
        Serial.println("OFF");
    }
    Serial.println("===================");
    Serial.println();
}

// ===========================================
// Setup
// ===========================================

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    delay(100);

    // Initialize watchdog (ESP-IDF v5.x API)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_task_wdt_deinit();  // WDT may already be initialized
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Initialize OLED first for visual feedback
    Serial.println("Initializing OLED...");
    u8g2.begin();
    displayMessage("CO2 Monitor v3", "Starting...");
    delay(500);

    Serial.println();
    Serial.println("========================================");
    Serial.print("SCD41 CO2 Monitor v3 - ");
    Serial.println(deviceName);
    Serial.print("Endpoint: ");
    Serial.println(apiEndpoint);
    Serial.println("Mode: Periodic (ASC disabled, FRC enabled)");
    Serial.println("Features: OLED + IR Blaster");
    Serial.println("========================================");
    Serial.println();
    Serial.println("IR commands: on, off, spamon, spamoff, stop, help");
    Serial.println("Hold BOOT button 3 seconds to calibrate");
    Serial.println();

    // Initialize IR
    irsend.begin();

    // Connect WiFi
    displayMessage("Connecting WiFi...");
    connectWiFi();

    // Initialize I2C and sensor
    displayMessage("Init sensor...");
    Wire.begin(I2C_SDA, I2C_SCL);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(30);

    // Stop any running measurement
    sensor.stopPeriodicMeasurement();
    delay(500);

    // Configure sensor
    int16_t error;

    // Set altitude for pressure compensation
    error = sensor.setSensorAltitude(SENSOR_ALTITUDE_METERS);
    if (error != 0) {
        Serial.print("setSensorAltitude error: ");
        Serial.println(error);
    } else {
        Serial.print("Altitude set: ");
        Serial.print(SENSOR_ALTITUDE_METERS);
        Serial.println(" m");
    }

    // Set temperature offset
    error = sensor.setTemperatureOffset(TEMPERATURE_OFFSET_C);
    if (error != 0) {
        Serial.print("setTemperatureOffset error: ");
        Serial.println(error);
    } else {
        Serial.print("Temperature offset set: ");
        Serial.print(TEMPERATURE_OFFSET_C, 1);
        Serial.println(" C");
    }

    // Verify sensor and get serial number
    uint64_t serialNumber = 0;
    error = sensor.getSerialNumber(serialNumber);
    if (error != 0) {
        Serial.println("ERROR: SCD41 not found! Check wiring.");
        displayMessage("Sensor Error!", "Check wiring");
        sendEvent(EVENT_CRITICAL, "SCD41 sensor not found at startup");
        displayError = true;
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);

        displayMessage("Sensor ready!");
        delay(1000);

        char startupMsg[80];
        snprintf(startupMsg, sizeof(startupMsg),
                 "Sensor started, serial: %08X%08X, altitude: %dm",
                 (uint32_t)(serialNumber >> 32),
                 (uint32_t)(serialNumber & 0xFFFFFFFF),
                 SENSOR_ALTITUDE_METERS);
        sendEvent(EVENT_INFO, startupMsg);
    }

    // Start periodic measurement mode
    error = sensor.startPeriodicMeasurement();
    if (error != 0) {
        Serial.print("startPeriodicMeasurement error: ");
        Serial.println(error);
        sendEvent(EVENT_CRITICAL, "Failed to start periodic measurement");
    } else {
        Serial.println("Periodic measurement started");
    }

    // Disable ASC - relying on manual FRC calibration
    sensor.setAutomaticSelfCalibrationEnabled(false);
    Serial.println("ASC disabled (using manual FRC calibration)");

    // Initialize FRC module
    frcInit();

    // Set initial timing
    lastMeasurementTime = millis();
    lastDisplayUpdate = millis();

    // Show waiting countdown for first reading
    displayWaitingCountdown(MEASUREMENT_INTERVAL_MS);
    Serial.println();
    Serial.println("Ready. First reading in 60 seconds.");
    Serial.println();
}

// ===========================================
// Main loop
// ===========================================

void loop() {
    // Reset watchdog
    esp_task_wdt_reset();

    unsigned long now = millis();

    // Handle serial commands (IR control)
    handleSerialCommands();

    // Handle IR spam mode
    if (irSpamming && (now - lastIrSpam >= IR_SPAM_INTERVAL_MS)) {
        lastIrSpam = now;
        if (irSpamOn) {
            sendIROn();
            Serial.println("[IR] ON signal sent");
        } else {
            sendIROff();
            Serial.println("[IR] OFF signal sent");
        }
    }

    // Update display periodically (for clock, WiFi status, etc.)
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL_MS) {
        lastDisplayUpdate = now;
        if (displayWaiting) {
            // Show countdown during waiting phase
            unsigned long elapsed = now - lastMeasurementTime;
            unsigned long remaining = elapsed < MEASUREMENT_INTERVAL_MS ?
                                      MEASUREMENT_INTERVAL_MS - elapsed : 0;
            displayWaitingCountdown(remaining);
        } else {
            updateDisplay();
        }
    }

    // Check for FRC button press
    if (frcCheckButton(sensor, frcEventCallback, frcDisplayUpdate)) {
        // FRC was performed, restart periodic measurement
        sensor.startPeriodicMeasurement();
        lastMeasurementTime = millis();
        displayMessage("Calibration", "Complete!");
        delay(2000);
        return;
    }

    // Check if it's time for a measurement
    if (now - lastMeasurementTime < MEASUREMENT_INTERVAL_MS) {
        delay(50);
        return;
    }
    lastMeasurementTime = now;

    // Read measurement
    uint16_t co2 = 0;
    float temp = 0.0;
    float humidity = 0.0;

    bool dataReady = false;
    int16_t error = sensor.getDataReadyStatus(dataReady);

    if (error != 0) {
        Serial.print("getDataReadyStatus error: ");
        Serial.println(error);
        totalI2CErrors++;
        consecutiveI2CFailures++;
        displayError = true;
        updateDisplay();

        if (consecutiveI2CFailures >= 3) {
            sendEvent(EVENT_WARNING, "Attempting I2C recovery");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
                displayError = false;
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed");
            }
        }
        return;
    }

    if (!dataReady) {
        Serial.println("Data not ready (unexpected at 60s interval)");
        return;
    }

    error = sensor.readMeasurement(co2, temp, humidity);

    if (error != 0) {
        Serial.print("readMeasurement error: ");
        Serial.println(error);
        totalI2CErrors++;
        consecutiveI2CFailures++;
        displayError = true;
        updateDisplay();

        char errMsg[64];
        snprintf(errMsg, sizeof(errMsg), "Read failed, error: %d", error);
        sendEvent(EVENT_ERROR, errMsg);

        if (consecutiveI2CFailures >= 3) {
            sendEvent(EVENT_WARNING, "Attempting I2C recovery");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
                displayError = false;
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed");
            }
        }
        return;
    }

    // Successful read
    consecutiveI2CFailures = 0;
    totalMeasurements++;
    displayError = false;
    displayWaiting = false;

    // Update display values
    displayCO2 = co2;
    displayTemp = temp;
    displayHumidity = humidity;
    updateDisplay();

    // Sanity check
    if (co2 < 300 || co2 > 10000) {
        char warnMsg[48];
        snprintf(warnMsg, sizeof(warnMsg), "Unusual CO2: %d ppm", co2);
        sendEvent(EVENT_WARNING, warnMsg);
    }

    // Print reading
    Serial.print("CO2: ");
    Serial.print(co2);
    Serial.print(" ppm | Temp: ");
    Serial.print(temp, 1);
    Serial.print(" C | Humidity: ");
    Serial.print(humidity, 1);
    Serial.println(" %");

    // Send to API
    if (sendReading(co2, temp, humidity)) {
        successfulUploads++;
        consecutiveUploadFailures = 0;
        flashLED(1);
    } else {
        consecutiveUploadFailures++;
        flashLED(3);

        if (consecutiveUploadFailures >= 5) {
            char errMsg[48];
            snprintf(errMsg, sizeof(errMsg), "%lu consecutive upload failures",
                     consecutiveUploadFailures);
            sendEvent(EVENT_ERROR, errMsg);
        }
    }

    // Periodic diagnostics and health report
    if (totalMeasurements % 100 == 0) {
        printDiagnostics();

        char healthMsg[128];
        snprintf(healthMsg, sizeof(healthMsg),
                 "Health: %lu measurements, %.1f%% success, %lu I2C errors, %lu WiFi reconnects",
                 totalMeasurements,
                 totalMeasurements > 0 ? (100.0 * successfulUploads / totalMeasurements) : 0.0,
                 totalI2CErrors,
                 totalWiFiReconnects);
        sendEvent(EVENT_INFO, healthMsg);
    }
}
