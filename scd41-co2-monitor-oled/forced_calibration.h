#ifndef FORCED_CALIBRATION_H
#define FORCED_CALIBRATION_H

#include <Arduino.h>
#include <SensirionI2cScd4x.h>

// ============================================
// Configuration - adjust these as needed
// ============================================

#define FRC_BUTTON_PIN 0              // GPIO0 - BOOT button on most ESP32 boards
#define FRC_LED_PIN 2                 // Same LED as main code
#define FRC_REFERENCE_PPM 440         // Outdoor CO2 reference for Houston/Clear Lake area
#define FRC_HOLD_TIME_MS 3000         // Hold button for 3 seconds to trigger
#define FRC_WARMUP_DURATION_MS 300000 // 5 minutes warmup
#define FRC_WARMUP_INTERVAL_MS 30000  // Reading every 30 seconds during warmup

// ============================================
// Internal state
// ============================================

static bool _frcInitialized = false;

// ============================================
// LED helper functions
// ============================================

static void _frcFlashLED(int times, int onDuration = 100, int offDuration = 100) {
    for (int i = 0; i < times; i++) {
        digitalWrite(FRC_LED_PIN, HIGH);
        delay(onDuration);
        digitalWrite(FRC_LED_PIN, LOW);
        if (i < times - 1) delay(offDuration);
    }
}

static void _frcSlowFlash(int times) {
    _frcFlashLED(times, 400, 300);
}

static void _frcRapidFlash(int times) {
    _frcFlashLED(times, 80, 80);
}

// ============================================
// Callback type for event logging
// ============================================

// Matches the EventType enum from main code
enum FRCEventType {
    FRC_EVENT_INFO,
    FRC_EVENT_WARNING,
    FRC_EVENT_ERROR,
    FRC_EVENT_CRITICAL
};

// Function pointer type for sending events to server
// Set to nullptr if you don't want server logging
typedef bool (*FRCEventCallback)(int eventType, const char* message);

// ============================================
// Initialize FRC - call in setup()
// ============================================

void frcInit() {
    pinMode(FRC_BUTTON_PIN, INPUT_PULLUP);
    _frcInitialized = true;
    Serial.println("[FRC] Forced recalibration module initialized");
    Serial.printf("[FRC] Hold BOOT button for %d seconds to trigger\n", FRC_HOLD_TIME_MS / 1000);
}

// ============================================
// Main FRC check - call at start of loop()
// Returns true if FRC was performed
// ============================================

bool frcCheckButton(SensirionI2cScd4x &sensor, FRCEventCallback logEvent = nullptr) {
    if (!_frcInitialized) return false;
    
    // Check if button is pressed
    if (digitalRead(FRC_BUTTON_PIN) != LOW) {
        return false;
    }
    
    // Button is pressed - start timing for hold duration
    Serial.println("[FRC] Button pressed, hold for 3 seconds to start calibration...");
    
    unsigned long pressStart = millis();
    int dotsShown = 0;
    
    // Wait for 3-second hold
    while (digitalRead(FRC_BUTTON_PIN) == LOW) {
        unsigned long held = millis() - pressStart;
        
        // Show progress dots every 500ms
        if (held / 500 > dotsShown) {
            Serial.print(".");
            dotsShown++;
        }
        
        // Check if held long enough
        if (held >= FRC_HOLD_TIME_MS) {
            Serial.println(" GO!");
            break;
        }
        
        delay(50);
    }
    
    // Check if released too early
    if (digitalRead(FRC_BUTTON_PIN) != LOW) {
        Serial.println("\n[FRC] Button released too early, cancelled");
        return false;
    }
    
    // ========================================
    // FRC TRIGGERED - Point of no return
    // ========================================
    
    Serial.println("\n[FRC] ========================================");
    Serial.println("[FRC] FORCED RECALIBRATION STARTING");
    Serial.printf("[FRC] Reference: %d ppm\n", FRC_REFERENCE_PPM);
    Serial.printf("[FRC] Warmup: %d minutes\n", FRC_WARMUP_DURATION_MS / 60000);
    Serial.println("[FRC] ========================================");
    
    // Signal: 5 quick flashes = acknowledged
    _frcFlashLED(5, 150, 150);
    
    // Log to server if available
    if (logEvent) {
        char msg[128];
        snprintf(msg, sizeof(msg), "FRC started - %d min warmup, %d ppm reference", 
                 FRC_WARMUP_DURATION_MS / 60000, FRC_REFERENCE_PPM);
        logEvent(FRC_EVENT_INFO, msg);
    }
    
    // ========================================
    // WARMUP PHASE - 5 minutes of readings
    // ========================================
    
    Serial.println("[FRC] Starting warmup phase...");
    Serial.println("[FRC] Keep sensor in fresh outdoor air!");
    
    // Wake sensor
    int16_t error = sensor.wakeUp();
    if (error) {
        Serial.println("[FRC] ERROR: Could not wake sensor");
        _frcRapidFlash(10);
        if (logEvent) logEvent(FRC_EVENT_ERROR, "FRC failed - could not wake sensor");
        goto cleanup;
    }
    delay(30);
    
    // Stop any periodic measurement
    sensor.stopPeriodicMeasurement();
    delay(500);
    
    // Take readings during warmup
    {
        unsigned long warmupStart = millis();
        int readingCount = 0;
        float avgCO2 = 0;
        
        while (millis() - warmupStart < FRC_WARMUP_DURATION_MS) {
            unsigned long elapsed = millis() - warmupStart;
            unsigned long remaining = FRC_WARMUP_DURATION_MS - elapsed;
            
            // Take a single-shot reading
            uint16_t co2 = 0;
            float temp = 0, humidity = 0;
            
            error = sensor.measureSingleShot();
            if (error) {
                Serial.printf("[FRC] Warmup measurement error: %d\n", error);
                _frcFlashLED(2, 50, 50);  // Quick double flash for error
            } else {
                delay(5000);  // Wait for measurement
                
                bool dataReady = false;
                sensor.getDataReadyStatus(dataReady);
                
                if (dataReady) {
                    error = sensor.readMeasurement(co2, temp, humidity);
                    if (error == 0 && co2 > 0) {
                        readingCount++;
                        avgCO2 = ((avgCO2 * (readingCount - 1)) + co2) / readingCount;
                        
                        Serial.printf("[FRC] Warmup %d/%d: CO2=%d ppm (avg=%.0f) | %lu sec remaining\n",
                                      readingCount,
                                      FRC_WARMUP_DURATION_MS / FRC_WARMUP_INTERVAL_MS,
                                      co2, avgCO2,
                                      remaining / 1000);
                        
                        // Single LED blink per successful reading
                        _frcFlashLED(1, 100, 0);
                    }
                }
            }
            
            // Wait for next interval (accounting for measurement time)
            unsigned long nextReading = warmupStart + ((readingCount + 1) * FRC_WARMUP_INTERVAL_MS);
            while (millis() < nextReading && millis() - warmupStart < FRC_WARMUP_DURATION_MS) {
                delay(100);
            }
        }
        
        Serial.println("[FRC] Warmup complete!");
        Serial.printf("[FRC] Took %d readings, average CO2: %.0f ppm\n", readingCount, avgCO2);
        
        // Sanity check - warn if readings are far from reference
        if (readingCount > 0) {
            float diff = avgCO2 - FRC_REFERENCE_PPM;
            if (diff > 100 || diff < -100) {
                Serial.printf("[FRC] WARNING: Average (%.0f) differs from reference (%d) by %.0f ppm\n",
                              avgCO2, FRC_REFERENCE_PPM, diff);
                Serial.println("[FRC] Large correction will be applied - ensure you're actually outside!");
                
                if (logEvent) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "FRC warmup avg %.0f ppm, reference %d ppm (diff: %.0f)",
                             avgCO2, FRC_REFERENCE_PPM, diff);
                    logEvent(FRC_EVENT_WARNING, msg);
                }
            }
        }
    }
    
    // ========================================
    // PERFORM FORCED RECALIBRATION
    // ========================================
    
    // Signal: 3 slow flashes = about to calibrate
    Serial.println("[FRC] Performing forced recalibration...");
    _frcSlowFlash(3);
    
    {
        uint16_t frcCorrection = 0;
        error = sensor.performForcedRecalibration(FRC_REFERENCE_PPM, frcCorrection);
        
        if (error != 0) {
            Serial.printf("[FRC] ERROR: FRC command failed with error %d\n", error);
            _frcRapidFlash(10);
            if (logEvent) {
                char msg[64];
                snprintf(msg, sizeof(msg), "FRC command failed, error code: %d", error);
                logEvent(FRC_EVENT_ERROR, msg);
            }
            goto cleanup;
        }
        
        if (frcCorrection == 0xFFFF) {
            Serial.println("[FRC] ERROR: FRC failed - sensor returned 0xFFFF");
            Serial.println("[FRC] This usually means sensor wasn't measuring before FRC");
            _frcRapidFlash(10);
            if (logEvent) logEvent(FRC_EVENT_ERROR, "FRC failed - sensor returned 0xFFFF");
            goto cleanup;
        }
        
        // Success! Calculate actual correction
        int16_t correction = (int16_t)(frcCorrection - 0x8000);
        
        Serial.println("[FRC] ========================================");
        Serial.println("[FRC] CALIBRATION SUCCESSFUL!");
        Serial.printf("[FRC] Correction applied: %d ppm\n", correction);
        Serial.println("[FRC] ========================================");
        
        // Signal: 2 long flashes = success
        _frcSlowFlash(2);
        
        if (logEvent) {
            char msg[128];
            snprintf(msg, sizeof(msg), "FRC successful! Correction: %d ppm, reference: %d ppm",
                     correction, FRC_REFERENCE_PPM);
            logEvent(FRC_EVENT_INFO, msg);
        }
    }
    
cleanup:
    // Power down sensor
    sensor.powerDown();
    
    // Wait for button release before returning
    Serial.println("[FRC] Release button to continue normal operation...");
    while (digitalRead(FRC_BUTTON_PIN) == LOW) {
        delay(50);
    }
    delay(200);  // Debounce on release
    
    Serial.println("[FRC] Returning to normal operation\n");
    return true;
}

#endif // FORCED_CALIBRATION_H
