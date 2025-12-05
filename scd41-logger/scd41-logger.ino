#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "secrets.h"  // WiFi creds and API token

// Config (not sensitive - safe to commit)
const char* endpoint = "https://www.dropbop.xyz/api/sensor";
const char* deviceName = "office";

// Sensor
SensirionI2cScd4x sensor;

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

static char errorMessage[64];
static int16_t error;

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("WiFi connection failed!");
    }
}

void sendData(uint16_t co2, float temp, float humidity) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, attempting reconnect...");
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Still no WiFi, skipping upload.");
            return;
        }
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
    
    if (httpCode > 0) {
        Serial.print("HTTP Response: ");
        Serial.println(httpCode);
        String response = http.getString();
        Serial.println(response);
    } else {
        Serial.print("HTTP Error: ");
        Serial.println(http.errorToString(httpCode));
    }
    
    http.end();
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    
    Serial.println();
    Serial.println("=== ESP32 SCD41 CO2 Sensor + WiFi ===");
    
    // Connect to WiFi
    connectWiFi();
    
    // Initialize I2C
    Wire.begin(21, 22);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    
    delay(30);
    
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
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);
        Serial.println("Sensor ready!");
    }
    
    Serial.println();
}

void loop() {
    uint16_t co2 = 0;
    float temp = 0.0;
    float humidity = 0.0;
    
    // Wake sensor
    error = sensor.wakeUp();
    if (error != NO_ERROR) {
        Serial.print("wakeUp error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        delay(20000);
        return;
    }
    
    // First measurement (discard)
    error = sensor.measureSingleShot();
    if (error != NO_ERROR) {
        Serial.print("measureSingleShot error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        delay(20000);
        return;
    }
    
    // Actual measurement
    error = sensor.measureAndReadSingleShot(co2, temp, humidity);
    if (error != NO_ERROR) {
        Serial.print("measureAndReadSingleShot error: ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        delay(20000);
        return;
    }
    
    // Print locally
    Serial.println("=== Reading ===");
    Serial.printf("CO2: %d ppm | Temp: %.1f C | Humidity: %.1f %%\n", co2, temp, humidity);
    
    // Send to server
    sendData(co2, temp, humidity);
    
    Serial.println("Waiting 20 seconds...\n");
    delay(20000);
}
