/*
 * SCD41 CO2 Monitor - Local Version
 *
 * Sends readings every 30 seconds to a local Flask API.
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
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "secrets.h"

// Config
const unsigned long MEASUREMENT_INTERVAL_MS = 30000;  // 30 seconds
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

// Stats
static uint32_t totalMeasurements = 0;
static uint32_t successfulUploads = 0;
static uint32_t consecutiveI2CFailures = 0;

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
    }
}

bool sendReading(uint16_t co2, float temp, float humidity) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, reconnecting...");
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }
    }

    HTTPClient http;
    String url = String(apiEndpoint) + "/api/sensor";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    String payload = "{\"device\":\"" + String(deviceName) +
                     "\",\"co2\":" + String(co2) +
                     ",\"temp\":" + String(temp, 1) +
                     ",\"humidity\":" + String(humidity, 1) + "}";

    Serial.print("POST ");
    Serial.print(url);
    Serial.print(" -> ");
    Serial.println(payload);

    int httpCode = http.POST(payload);
    http.end();

    if (httpCode == 200) {
        Serial.println("OK");
        return true;
    } else {
        Serial.print("Failed: ");
        Serial.println(httpCode);
        return false;
    }
}

bool recoverI2C() {
    Serial.println("Attempting I2C recovery...");

    Wire.end();
    delay(100);

    pinMode(21, OUTPUT);
    pinMode(22, OUTPUT);

    digitalWrite(21, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(22, HIGH);
        delayMicroseconds(5);
        digitalWrite(22, LOW);
        delayMicroseconds(5);
    }

    digitalWrite(21, LOW);
    delayMicroseconds(5);
    digitalWrite(22, HIGH);
    delayMicroseconds(5);
    digitalWrite(21, HIGH);

    delay(100);

    Wire.begin(21, 22);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);
    delay(50);

    error = sensor.wakeUp();
    if (error != NO_ERROR) return false;

    delay(30);

    uint64_t serialNumber = 0;
    error = sensor.getSerialNumber(serialNumber);
    return (error == NO_ERROR);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    delay(100);

    pinMode(LED_PIN, OUTPUT);

    Serial.println();
    Serial.println("================================");
    Serial.print("SCD41 CO2 Monitor - Local (");
    Serial.print(deviceName);
    Serial.println(")");
    Serial.print("Endpoint: ");
    Serial.println(apiEndpoint);
    Serial.println("================================");
    Serial.println();

    connectWiFi();

    // Initialize sensor
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
    } else {
        Serial.print("SCD41 serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);
    }

    sensor.powerDown();
    Serial.println("Ready.\n");
}

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
        consecutiveI2CFailures++;

        if (consecutiveI2CFailures >= 3) {
            if (recoverI2C()) {
                Serial.println("I2C recovered");
                consecutiveI2CFailures = 0;
            }
        }

        delay(MEASUREMENT_INTERVAL_MS);
        return;
    }

    // Measure (~5 seconds)
    error = sensor.measureAndReadSingleShot(co2, temp, humidity);
    if (error != NO_ERROR) {
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.print("Measurement error: ");
        Serial.println(errorMessage);
        consecutiveI2CFailures++;

        if (consecutiveI2CFailures >= 3) {
            if (recoverI2C()) {
                Serial.println("I2C recovered");
                consecutiveI2CFailures = 0;
            }
        }

        sensor.powerDown();
        delay(MEASUREMENT_INTERVAL_MS);
        return;
    }

    consecutiveI2CFailures = 0;
    sensor.powerDown();
    totalMeasurements++;

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
        flashLED(1);
    } else {
        flashLED(3);
    }

    // Stats every 100 readings
    if (totalMeasurements % 100 == 0) {
        Serial.print("[Stats] ");
        Serial.print(totalMeasurements);
        Serial.print(" readings, ");
        Serial.print(successfulUploads);
        Serial.print(" uploads (");
        Serial.print(100.0 * successfulUploads / totalMeasurements, 1);
        Serial.println("%)");
    }

    // Wait (subtract ~5s for measurement time)
    unsigned long waitTime = MEASUREMENT_INTERVAL_MS > 5000 ? MEASUREMENT_INTERVAL_MS - 5000 : 0;
    delay(waitTime);
}
