/*
 * Whynter AC IR Blaster
 * 
 * Wiring (simple direct drive):
 *   GPIO4 -> 100Ω resistor -> IR LED anode (long leg)
 *   IR LED cathode (short leg) -> GND
 * 
 * Press BOOT button (GPIO0) to send AC_On signal
 * 
 * Dependencies: IRremoteESP8266 library
 *   Install via Arduino Library Manager or PlatformIO
 */

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// Pin definitions
const uint16_t kIrLedPin = 4;      // GPIO4 for IR LED
const uint8_t kBootButtonPin = 0;  // GPIO0 is the BOOT button

// IR sender instance
IRsend irsend(kIrLedPin);

// Whynter AC_On raw timing data (from Flipper Zero capture)
// 38kHz carrier, timings in microseconds
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

// AC_Off raw timing data
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

// Button state tracking for debounce
bool spamming = false;
bool acIsOn = false;

// Spam timing
unsigned long lastSpamTime = 0;
const unsigned long spamInterval = 250;  // Send signal every 250ms

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println();
  Serial.println("=================================");
  Serial.println("Whynter AC IR Blaster");
  Serial.println("=================================");
  Serial.println("Press BOOT to toggle spamming ON/OFF signals");
  Serial.println();
  
  // Initialize IR sender
  irsend.begin();
  
  // Initialize BOOT button with internal pullup
  pinMode(kBootButtonPin, INPUT_PULLUP);
  
  // Quick LED test - blink the IR LED (visible through phone camera)
  Serial.println("Testing IR LED - check with phone camera...");
  for (int i = 0; i < 5; i++) {
    irsend.sendRaw(AC_ON_RAW, 10, 38);
    delay(200);
  }
  Serial.println("IR LED test complete.");
  Serial.println();
}

void loop() {
  // Simple button check (same pattern that works in your test)
  if (digitalRead(kBootButtonPin) == LOW) {
    Serial.println("Button pressed!");
    
    if (!spamming) {
      spamming = true;
      acIsOn = true;
      Serial.println(">>> SPAMMING AC ON - press again to switch to OFF");
    } else if (acIsOn) {
      acIsOn = false;
      Serial.println(">>> SPAMMING AC OFF - press again to stop");
    } else {
      spamming = false;
      Serial.println(">>> STOPPED");
    }
    Serial.println();
    
    delay(300);  // Simple debounce
  }
  
  // Spam the signal if active
  if (spamming && (millis() - lastSpamTime) >= spamInterval) {
    if (acIsOn) {
      irsend.sendRaw(AC_ON_RAW, AC_ON_RAW_LEN, 38);
      Serial.println("ON!");
    } else {
      irsend.sendRaw(AC_OFF_RAW, AC_OFF_RAW_LEN, 38);
      Serial.println("OFF!");
    }
    lastSpamTime = millis();
  }
}