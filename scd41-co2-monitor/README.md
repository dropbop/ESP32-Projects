# SCD41 CO2 Monitor

ESP32-based CO2/temperature/humidity monitor. Posts readings to a remote API every 60 seconds. Includes forced recalibration via BOOT button.

## Hardware

- ESP32 Dev Module (CP2102 or similar)
- [Sensirion SCD41](https://sensirion.com/products/catalog/SCD41) CO2 sensor

### Wiring

| SCD41 | ESP32 |
|-------|-------|
| VDD (Red) | 3V3 |
| GND (Black) | GND |
| SDA (Green) | GPIO 21 |
| SCL (Yellow) | GPIO 22 |

Built-in LED on GPIO 2 used for status feedback.

## Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support
2. Install library: **Sensirion I2C SCD4x** (Library Manager)
3. Copy `secrets.h.example` to `secrets.h` and fill in:
   ```cpp
   const char* ssid = "your_wifi";
   const char* password = "your_password";
   const char* sensorToken = "your_api_token";
   ```
4. Update `deviceName` and endpoints in main `.ino` if needed
5. Upload to ESP32 Dev Module

## Files

| File | Purpose |
|------|---------|
| `scd41-co2-monitor.ino` | Main code - sensor reading, WiFi, HTTP posting, I2C recovery |
| `secrets.h` | WiFi creds and API token (gitignored) |
| `forced_calibration.h` | Optional FRC module triggered by BOOT button |

## Features

**Sensor reading:** Single-shot mode every 60s. Powers down sensor between readings.

**Error handling:** Tracks consecutive I2C failures, attempts bus recovery after 3 failures. Logs events to server.

**LED feedback:**
- 1 flash = successful upload
- 3 flashes = upload failed
- Slow blink = connecting to WiFi

**Forced Recalibration (FRC):**
Hold BOOT button for 3 seconds to trigger. Requires 5 min warmup in fresh outdoor air, then calibrates to 440 ppm reference. Configurable in `forced_calibration.h`.

## API

**Sensor data** (`POST /api/sensor`):
```json
{"device": "office", "co2": 850, "temp": 22.1, "humidity": 45.3}
```

**Event logging** (`POST /api/sensor/log`):
```json
{"device": "office", "event_type": "error", "message": "...", "uptime": 3600, "heap": 200000}
```

Both require `X-Sensor-Token` header.
