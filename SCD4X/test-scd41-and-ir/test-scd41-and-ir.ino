/*
 * Combined SCD41 CO2 Monitor + IR Blaster Test
 *
 * Wiring:
 *   SCD41: SDA -> GPIO 21, SCL -> GPIO 22, VCC -> 3.3V, GND -> GND
 *   IR LED: GPIO 4 -> 100Ω resistor -> IR LED anode, cathode -> GND
 *   Button: BOOT button (GPIO 0) - built-in on most ESP32 dev boards
 *
 * Behavior:
 *   - Prints CO2, temperature, and humidity every 10 seconds
 *   - Press BOOT button to toggle IR LED transmission on/off
 */

#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// Pin definitions
const uint8_t IR_LED_PIN = 4;
const uint8_t BOOT_BUTTON_PIN = 0;
const uint8_t I2C_SDA = 21;
const uint8_t I2C_SCL = 22;

// Timing constants
const unsigned long SCD41_READ_INTERVAL = 10000;  // 10 seconds
const unsigned long IR_SPAM_INTERVAL = 250;       // 250ms between IR bursts
const unsigned long DEBOUNCE_DELAY = 300;         // 300ms debounce

// IR signal data (Whynter AC ON signal from your existing project)
const uint16_t AC_ON_RAW[] = {
  8550, 4306, 550, 1578, 550, 1578, 574, 530, 550, 530, 550, 530,
  550, 530, 550, 530, 574, 530, 550, 530, 550, 530, 550, 530,
  550, 1602, 550, 530, 550, 1578, 574, 1578, 550, 1578, 550, 530,
  550, 1602, 550, 530, 550, 1578, 550, 530, 574, 530, 550, 530,
  550, 530, 550, 1578, 550, 554, 550, 530, 550, 1578, 550, 1602,
  550, 1578, 550, 1578, 550, 530, 574, 530, 550, 530, 550, 530,
  550, 530, 550, 530, 550, 554, 550, 530, 550, 1578, 550, 1578,
  550, 1602, 550, 1578, 550, 1578, 550, 1578, 574, 1578, 550, 1578,
  550
};
const uint16_t AC_ON_RAW_LEN = sizeof(AC_ON_RAW) / sizeof(AC_ON_RAW[0]);

// Objects
SensirionI2cScd4x sensor;
IRsend irsend(IR_LED_PIN);

// State variables
bool irTransmitting = false;
unsigned long lastScd41Read = 0;
unsigned long lastIrSpam = 0;
unsigned long lastButtonPress = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=================================");
  Serial.println("SCD41 + IR Blaster Test");
  Serial.println("=================================");

  // Initialize button
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Initialize IR
  irsend.begin();
  Serial.println("IR LED initialized on GPIO 4");

  // Initialize I2C and SCD41
  Wire.begin(I2C_SDA, I2C_SCL);
  sensor.begin(Wire, SCD41_I2C_ADDR_62);

  Serial.println("Initializing SCD41...");

  uint16_t error;

  // Wake up and reset sensor
  sensor.wakeUp();
  delay(30);

  error = sensor.stopPeriodicMeasurement();
  if (error) {
    Serial.println("Warning: stopPeriodicMeasurement failed");
  }
  delay(500);

  error = sensor.reinit();
  if (error) {
    Serial.println("Warning: reinit failed");
  }
  delay(30);

  // Verify sensor connection
  uint64_t serialNumber;
  error = sensor.getSerialNumber(serialNumber);
  if (error) {
    Serial.println("ERROR: Could not get SCD41 serial number!");
    Serial.println("Check wiring: SDA->GPIO21, SCL->GPIO22");
  } else {
    Serial.print("SCD41 Serial: ");
    Serial.println((unsigned long)serialNumber);
  }

  Serial.println();
  Serial.println("Ready! Press BOOT button to toggle IR transmission.");
  Serial.println("SCD41 readings every 10 seconds.");
  Serial.println("=================================");
  Serial.println();
}

void loop() {
  unsigned long now = millis();

  // Handle button press (toggle IR transmission)
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    if (now - lastButtonPress > DEBOUNCE_DELAY) {
      lastButtonPress = now;
      irTransmitting = !irTransmitting;

      Serial.println();
      if (irTransmitting) {
        Serial.println(">>> IR TRANSMISSION: ON <<<");
      } else {
        Serial.println(">>> IR TRANSMISSION: OFF <<<");
      }
      Serial.println();
    }
  }

  // Send IR signal if transmitting
  if (irTransmitting && (now - lastIrSpam > IR_SPAM_INTERVAL)) {
    lastIrSpam = now;
    irsend.sendRaw(AC_ON_RAW, AC_ON_RAW_LEN, 38);
    Serial.println("[IR] Signal sent");
  }

  // Read SCD41 every 10 seconds
  if (now - lastScd41Read >= SCD41_READ_INTERVAL) {
    lastScd41Read = now;
    readAndPrintScd41();
  }
}

void readAndPrintScd41() {
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;

  // Wake sensor
  sensor.wakeUp();
  delay(30);

  // Take single-shot measurement (takes ~5 seconds)
  Serial.println("[SCD41] Taking measurement...");
  uint16_t error = sensor.measureAndReadSingleShot(co2, temperature, humidity);

  // Power down to save energy
  sensor.powerDown();

  if (error) {
    Serial.println("[SCD41] ERROR: Measurement failed!");
    return;
  }

  // Print results
  Serial.println("---------------------------");
  Serial.print("CO2:         ");
  Serial.print(co2);
  Serial.println(" ppm");

  Serial.print("Temperature: ");
  Serial.print(temperature, 1);
  Serial.println(" C");

  Serial.print("Humidity:    ");
  Serial.print(humidity, 1);
  Serial.println(" %");
  Serial.println("---------------------------");
}
