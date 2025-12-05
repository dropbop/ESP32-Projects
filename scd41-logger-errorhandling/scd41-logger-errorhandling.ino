#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "secrets.h"  // WiFi creds and API token

// Config (not sensitive - safe to commit)
const char* endpoint = "https://www.dropbop.xyz/api/sensor";
const char* deviceName = "office";

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
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
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

void sendData(uint16_t co2, float temp, float humidity) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, attempting reconnect...");
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Still no WiFi, skipping upload.");
            flashLED(3);  // Triple flash = failed
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
    
    if (httpCode == 200) {
        Serial.print("HTTP Response: ");
        Serial.println(httpCode);
        String response = http.getString();
        Serial.println(response);
        flashLED(1);  // Single flash = success
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
