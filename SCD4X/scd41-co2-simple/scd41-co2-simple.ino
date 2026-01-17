// Minimal SCD41 CO2 sensor test - serial output only
#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

SensirionI2cScd4x sensor;

void setup() {
    Serial.begin(115200);

    while (!Serial) {
        delay(100);
    }

    delay(1000);  // Give serial monitor time to connect

    Serial.println();
    Serial.println("=== SCD41 Simple Test ===");

    // Initialize I2C (GPIO 21 = SDA, GPIO 22 = SCL)
    Wire.begin(21, 22);
    sensor.begin(Wire, SCD41_I2C_ADDR_62);

    delay(30);  // Sensor power-up time

    // Wake up and stop any existing measurement
    sensor.wakeUp();
    delay(30);
    sensor.stopPeriodicMeasurement();
    delay(500);
    sensor.reinit();
    delay(30);

    // Get serial number to verify communication
    uint64_t serialNumber = 0;
    int16_t error = sensor.getSerialNumber(serialNumber);

    if (error != 0) {
        Serial.print("Error getting serial number: ");
        Serial.println(error);
        Serial.println("Check wiring! SDA=21, SCL=22");
    } else {
        Serial.print("SCD41 found! Serial: 0x");
        Serial.print((uint32_t)(serialNumber >> 32), HEX);
        Serial.println((uint32_t)(serialNumber & 0xFFFFFFFF), HEX);
    }

    Serial.println("Starting measurements...");
    Serial.println();
}

void loop() {
    uint16_t co2 = 0;
    float temp = 0.0;
    float humidity = 0.0;

    // Wake sensor
    sensor.wakeUp();
    delay(30);

    // Single-shot measurement (~5 seconds)
    int16_t error = sensor.measureAndReadSingleShot(co2, temp, humidity);

    if (error != 0) {
        Serial.print("Measurement error: ");
        Serial.println(error);
    } else {
        Serial.print("CO2: ");
        Serial.print(co2);
        Serial.print(" ppm | Temp: ");
        Serial.print(temp, 1);
        Serial.print(" C | Humidity: ");
        Serial.print(humidity, 1);
        Serial.println(" %");
    }

    // Power down between readings
    sensor.powerDown();

    delay(10000);  // Wait 10 seconds between readings
}
