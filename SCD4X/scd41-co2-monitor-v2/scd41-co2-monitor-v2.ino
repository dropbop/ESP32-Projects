/*
 * SCD41 CO2 Monitor - Local API Version
 * 
 * Sends readings every 60 seconds to a local Flask API.
 * Uses periodic measurement mode with ASC (automatic self-calibration) enabled.
 * 
 * Hardware:
 *   - ESP32 Dev Module
 *   - Sensirion SCD41 CO2 sensor (I2C)
 * 
 * Wiring:
 *   SCD41 SDA  -> GPIO 21
 *   SCD41 SCL  -> GPIO 22
 *   SCD41 VDD  -> 3V3
 *   SCD41 GND  -> GND
 * 
 * Calibration:
 *   ASC is enabled and handles long-term drift automatically, provided the
 *   sensor sees fresh outdoor air (~420-440 ppm) for at least 3 minutes
 *   once per week. For manual calibration, hold BOOT button for 3 seconds.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>
#include <esp_task_wdt.h>

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
// Adjust if you move the sensor to a different elevation
const uint16_t SENSOR_ALTITUDE_METERS = 15;

// Temperature offset compensation for self-heating
// Default is 4.0°C - calibrate against a reference thermometer and adjust
// Formula: offset = T_sensor - T_reference + current_offset
const float TEMPERATURE_OFFSET_C = 3.6;

// WiFi connection
const unsigned long WIFI_RETRY_DELAY_MS = 500;
const int WIFI_MAX_ATTEMPTS = 30;

// Watchdog timeout - resets ESP32 if loop hangs
const int WATCHDOG_TIMEOUT_SECONDS = 120;

// ===========================================
// Hardware
// ===========================================

#define LED_PIN 2
#define I2C_SDA 21
#define I2C_SCL 22

// ===========================================
// Global state
// ===========================================

SensirionI2cScd4x sensor;

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
        
        // Restart periodic measurement
        sensor.stopPeriodicMeasurement();
        delay(500);
        sensor.startPeriodicMeasurement();
        
        return true;
    }
    
    Serial.println("I2C recovery failed");
    return false;
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
    
    Serial.println();
    Serial.println("================================");
    Serial.print("SCD41 CO2 Monitor - ");
    Serial.println(deviceName);
    Serial.print("Endpoint: ");
    Serial.println(apiEndpoint);
    Serial.println("Mode: Periodic (ASC enabled)");
    Serial.println("================================");
    Serial.println();
    
    // Connect to WiFi
    connectWiFi();
    
    // Initialize I2C and sensor
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
    
    // Set temperature offset (library takes degrees directly)
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
        sendEvent(EVENT_CRITICAL, "SCD41 sensor not found at startup");
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);
        
        char startupMsg[80];
        snprintf(startupMsg, sizeof(startupMsg), 
                 "Sensor started, serial: %08X%08X, altitude: %dm",
                 (uint32_t)(serialNumber >> 32), 
                 (uint32_t)(serialNumber & 0xFFFFFFFF),
                 SENSOR_ALTITUDE_METERS);
        sendEvent(EVENT_INFO, startupMsg);
    }
    
    // Start periodic measurement mode
    // Sensor updates every 5 seconds internally
    error = sensor.startPeriodicMeasurement();
    if (error != 0) {
        Serial.print("startPeriodicMeasurement error: ");
        Serial.println(error);
        sendEvent(EVENT_CRITICAL, "Failed to start periodic measurement");
    } else {
        Serial.println("Periodic measurement started");
    }

    // Disable ASC - relying on manual FRC calibration instead
    sensor.setAutomaticSelfCalibrationEnabled(false);
    Serial.println("ASC disabled (using manual FRC calibration)");
    
    // Initialize FRC module
    frcInit();
    
    // Set initial timing
    lastMeasurementTime = millis();
    
    Serial.println();
    Serial.println("Ready. First reading in 60 seconds.");
    Serial.println("Hold BOOT button 3 seconds to force calibration.");
    Serial.println();
}

// ===========================================
// Main loop
// ===========================================

void loop() {
    // Reset watchdog
    esp_task_wdt_reset();
    
    // Check for FRC button press
    if (frcCheckButton(sensor, frcEventCallback)) {
        // FRC was performed, restart periodic measurement
        sensor.startPeriodicMeasurement();
        lastMeasurementTime = millis();
        return;
    }
    
    // Check if it's time for a measurement
    unsigned long now = millis();
    if (now - lastMeasurementTime < MEASUREMENT_INTERVAL_MS) {
        delay(100);
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
        
        if (consecutiveI2CFailures >= 3) {
            sendEvent(EVENT_WARNING, "Attempting I2C recovery");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
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
        
        char errMsg[64];
        snprintf(errMsg, sizeof(errMsg), "Read failed, error: %d", error);
        sendEvent(EVENT_ERROR, errMsg);
        
        if (consecutiveI2CFailures >= 3) {
            sendEvent(EVENT_WARNING, "Attempting I2C recovery");
            if (recoverI2C()) {
                sendEvent(EVENT_INFO, "I2C recovery successful");
                consecutiveI2CFailures = 0;
            } else {
                sendEvent(EVENT_CRITICAL, "I2C recovery failed");
            }
        }
        return;
    }
    
    // Successful read
    consecutiveI2CFailures = 0;
    totalMeasurements++;
    
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
