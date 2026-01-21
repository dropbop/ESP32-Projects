/*
 * Forced Recalibration Module for SCD41
 * 
 * Provides manual calibration via BOOT button when automatic self-calibration
 * isn't sufficient or you need a quick correction.
 * 
 * Usage:
 *   1. Take sensor outside to fresh air
 *   2. Hold BOOT button for 3 seconds
 *   3. Wait 5 minutes for warmup (LED blinks for each reading)
 *   4. Calibration completes automatically
 * 
 * This module is designed for sensors running in periodic measurement mode.
 * It stops periodic measurement, runs warmup in single-shot mode, performs
 * FRC, then the caller should restart periodic measurement.
 */

#ifndef FORCED_CALIBRATION_H
#define FORCED_CALIBRATION_H

#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <esp_task_wdt.h>

// ===========================================
// Configuration
// ===========================================

// GPIO0 - BOOT button on most ESP32 dev boards
#define FRC_BUTTON_PIN 0

// LED for feedback (same as main code)
#define FRC_LED_PIN 2

// Outdoor CO2 reference for Houston/Clear Lake area
// Global background is ~420 ppm, urban areas run 10-50 ppm higher
// Adjust if calibrating in a rural area (use 420) or near traffic (use 450+)
#define FRC_REFERENCE_PPM 440

// Hold button this long to trigger FRC
#define FRC_HOLD_TIME_MS 3000

// Warmup duration - datasheet requires minimum 3 minutes
// Using 5 minutes for better stabilization
#define FRC_WARMUP_DURATION_MS 300000

// Take a reading every 30 seconds during warmup
#define FRC_WARMUP_INTERVAL_MS 30000

// ===========================================
// State
// ===========================================

static bool _frcInitialized = false;

// ===========================================
// LED helpers
// ===========================================

static void _frcFlashLED(int times, int onMs = 100, int offMs = 100) {
    for (int i = 0; i < times; i++) {
        digitalWrite(FRC_LED_PIN, HIGH);
        delay(onMs);
        digitalWrite(FRC_LED_PIN, LOW);
        if (i < times - 1) delay(offMs);
    }
}

static void _frcSlowFlash(int times) {
    _frcFlashLED(times, 400, 300);
}

static void _frcRapidFlash(int times) {
    _frcFlashLED(times, 80, 80);
}

// ===========================================
// Callback type for event logging
// ===========================================

enum FRCEventType {
    FRC_EVENT_INFO = 0,
    FRC_EVENT_WARNING = 1,
    FRC_EVENT_ERROR = 2,
    FRC_EVENT_CRITICAL = 3
};

typedef bool (*FRCEventCallback)(int eventType, const char* message);

// ===========================================
// Initialize - call in setup()
// ===========================================

void frcInit() {
    pinMode(FRC_BUTTON_PIN, INPUT_PULLUP);
    _frcInitialized = true;
    Serial.println("[FRC] Module initialized");
    Serial.print("[FRC] Hold BOOT button ");
    Serial.print(FRC_HOLD_TIME_MS / 1000);
    Serial.println(" seconds to calibrate");
}

// ===========================================
// Main check - call at start of loop()
// Returns true if FRC was performed
// 
// IMPORTANT: After this returns true, caller must restart
// periodic measurement with sensor.startPeriodicMeasurement()
// ===========================================

bool frcCheckButton(SensirionI2cScd4x &sensor, FRCEventCallback logEvent = nullptr) {
    if (!_frcInitialized) return false;
    
    // Check if button is pressed (active low)
    if (digitalRead(FRC_BUTTON_PIN) != LOW) {
        return false;
    }
    
    // Button pressed - wait for hold duration
    Serial.println("[FRC] Button pressed, hold 3 seconds to calibrate...");
    
    unsigned long pressStart = millis();
    int dots = 0;
    
    while (digitalRead(FRC_BUTTON_PIN) == LOW) {
        unsigned long held = millis() - pressStart;

        // Keep watchdog happy during button hold
        esp_task_wdt_reset();

        // Progress dots
        if (held / 500 > dots) {
            Serial.print(".");
            dots++;
        }

        if (held >= FRC_HOLD_TIME_MS) {
            Serial.println(" GO!");
            break;
        }

        delay(50);
    }
    
    // Released too early?
    if (digitalRead(FRC_BUTTON_PIN) != LOW && (millis() - pressStart) < FRC_HOLD_TIME_MS) {
        Serial.println("\n[FRC] Released too early, cancelled");
        return false;
    }
    
    // ========================================
    // FRC TRIGGERED
    // ========================================
    
    Serial.println();
    Serial.println("[FRC] ========================================");
    Serial.println("[FRC] FORCED RECALIBRATION STARTING");
    Serial.print("[FRC] Reference: ");
    Serial.print(FRC_REFERENCE_PPM);
    Serial.println(" ppm");
    Serial.print("[FRC] Warmup: ");
    Serial.print(FRC_WARMUP_DURATION_MS / 60000);
    Serial.println(" minutes");
    Serial.println("[FRC] Keep sensor in fresh outdoor air!");
    Serial.println("[FRC] ========================================");
    
    // Acknowledge: 5 quick flashes
    _frcFlashLED(5, 150, 150);
    
    if (logEvent) {
        char msg[96];
        snprintf(msg, sizeof(msg), "FRC started - %d min warmup, %d ppm reference",
                 FRC_WARMUP_DURATION_MS / 60000, FRC_REFERENCE_PPM);
        logEvent(FRC_EVENT_INFO, msg);
    }
    
    // ========================================
    // STOP PERIODIC MEASUREMENT
    // ========================================
    
    int16_t error = sensor.stopPeriodicMeasurement();
    if (error != 0) {
        Serial.print("[FRC] stopPeriodicMeasurement error: ");
        Serial.println(error);
        // Continue anyway - might not have been running
    }
    delay(500);
    
    // ========================================
    // WARMUP PHASE
    // ========================================
    
    Serial.println("[FRC] Starting warmup...");
    
    unsigned long warmupStart = millis();
    int readingCount = 0;
    float avgCO2 = 0;
    bool warmupSuccess = true;
    
    while (millis() - warmupStart < FRC_WARMUP_DURATION_MS) {
        unsigned long elapsed = millis() - warmupStart;
        unsigned long remaining = FRC_WARMUP_DURATION_MS - elapsed;

        // Keep watchdog happy during 5-minute warmup
        esp_task_wdt_reset();

        // Single-shot measurement
        uint16_t co2 = 0;
        float temp = 0, humidity = 0;
        
        error = sensor.measureSingleShot();
        if (error != 0) {
            Serial.print("[FRC] measureSingleShot error: ");
            Serial.println(error);
            _frcFlashLED(2, 50, 50);
        } else {
            // Wait for measurement (~5 seconds)
            delay(5000);
            
            bool dataReady = false;
            sensor.getDataReadyStatus(dataReady);
            
            if (dataReady) {
                error = sensor.readMeasurement(co2, temp, humidity);
                if (error == 0 && co2 > 0) {
                    readingCount++;
                    avgCO2 = ((avgCO2 * (readingCount - 1)) + co2) / readingCount;
                    
                    Serial.print("[FRC] Reading ");
                    Serial.print(readingCount);
                    Serial.print(": CO2=");
                    Serial.print(co2);
                    Serial.print(" ppm (avg=");
                    Serial.print((int)avgCO2);
                    Serial.print(") | ");
                    Serial.print(remaining / 1000);
                    Serial.println("s remaining");
                    
                    _frcFlashLED(1, 100, 0);
                }
            }
        }
        
        // Wait for next reading interval
        unsigned long nextReading = warmupStart + ((readingCount + 1) * FRC_WARMUP_INTERVAL_MS);
        while (millis() < nextReading && millis() - warmupStart < FRC_WARMUP_DURATION_MS) {
            esp_task_wdt_reset();
            delay(100);
        }
    }
    
    Serial.println("[FRC] Warmup complete");
    Serial.print("[FRC] ");
    Serial.print(readingCount);
    Serial.print(" readings, average: ");
    Serial.print((int)avgCO2);
    Serial.println(" ppm");
    
    // Warn if readings differ significantly from reference
    if (readingCount > 0) {
        float diff = avgCO2 - FRC_REFERENCE_PPM;
        if (diff > 100 || diff < -100) {
            Serial.print("[FRC] WARNING: Average differs from reference by ");
            Serial.print((int)diff);
            Serial.println(" ppm");
            Serial.println("[FRC] Ensure you're actually in fresh outdoor air!");
            
            if (logEvent) {
                char msg[96];
                snprintf(msg, sizeof(msg), "FRC warmup avg %.0f ppm vs reference %d ppm (diff: %.0f)",
                         avgCO2, FRC_REFERENCE_PPM, diff);
                logEvent(FRC_EVENT_WARNING, msg);
            }
        }
    }
    
    // ========================================
    // PERFORM FRC
    // ========================================
    
    Serial.println("[FRC] Performing forced recalibration...");
    _frcSlowFlash(3);
    
    uint16_t frcCorrection = 0;
    error = sensor.performForcedRecalibration(FRC_REFERENCE_PPM, frcCorrection);
    
    if (error != 0) {
        Serial.print("[FRC] ERROR: FRC command failed: ");
        Serial.println(error);
        _frcRapidFlash(10);
        
        if (logEvent) {
            char msg[48];
            snprintf(msg, sizeof(msg), "FRC command failed, error: %d", error);
            logEvent(FRC_EVENT_ERROR, msg);
        }
        
        warmupSuccess = false;
    } else if (frcCorrection == 0xFFFF) {
        Serial.println("[FRC] ERROR: FRC failed (0xFFFF)");
        Serial.println("[FRC] Sensor wasn't measuring before FRC");
        _frcRapidFlash(10);
        
        if (logEvent) {
            logEvent(FRC_EVENT_ERROR, "FRC failed - sensor returned 0xFFFF");
        }
        
        warmupSuccess = false;
    } else {
        // Success!
        int16_t correction = (int16_t)(frcCorrection - 0x8000);
        
        Serial.println("[FRC] ========================================");
        Serial.println("[FRC] CALIBRATION SUCCESSFUL!");
        Serial.print("[FRC] Correction applied: ");
        Serial.print(correction);
        Serial.println(" ppm");
        Serial.println("[FRC] ========================================");
        
        _frcSlowFlash(2);
        
        if (logEvent) {
            char msg[96];
            snprintf(msg, sizeof(msg), "FRC successful! Correction: %d ppm, reference: %d ppm",
                     correction, FRC_REFERENCE_PPM);
            logEvent(FRC_EVENT_INFO, msg);
        }
    }
    
    // ========================================
    // CLEANUP
    // ========================================
    
    // Wait for button release
    Serial.println("[FRC] Release button to continue...");
    while (digitalRead(FRC_BUTTON_PIN) == LOW) {
        esp_task_wdt_reset();
        delay(50);
    }
    delay(200);  // Debounce
    
    Serial.println("[FRC] Returning to normal operation");
    Serial.println();
    
    // NOTE: Caller must restart periodic measurement!
    // We don't do it here so caller can handle any additional setup
    
    return true;
}

#endif // FORCED_CALIBRATION_H
