# SCD41 CO2 Monitor with OLED Display

ESP32-based CO2/temperature/humidity monitor with local OLED display. Buffers readings locally and sends batches to a remote API every 10 minutes.

## Hardware

- ESP32 Dev Module (CP2102 or similar)
- [Sensirion SCD41](https://sensirion.com/products/catalog/SCD41) CO2 sensor
- [Inland 1.3" 128x64 OLED](https://www.microcenter.com/product/643965) (SH1106 driver)

### Wiring

**SCD41 (I2C):**
| SCD41 | ESP32 |
|-------|-------|
| VDD (Red) | 3V3 |
| GND (Black) | GND |
| SDA (Green) | GPIO 21 |
| SCL (Yellow) | GPIO 22 |

**OLED (Software SPI):**
| OLED | ESP32 |
|------|-------|
| GND | GND |
| VCC | VIN (5V) |
| CLK | GPIO 25 |
| MOSI | GPIO 26 |
| RES | GPIO 12 |
| DC | GPIO 14 |
| CS | GPIO 27 |

Built-in LED on GPIO 2 used for status feedback.

## Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support
2. Install libraries (Library Manager):
   - **Sensirion I2C SCD4x**
   - **U8g2**
3. Copy `secrets.h.example` to `secrets.h` and fill in:
   ```cpp
   const char* ssid = "your_wifi";
   const char* password = "your_password";
   const char* sensorToken = "your_api_token";
   ```
4. Upload to ESP32 Dev Module

## Display Layout

```
┌────────────────────────┐
│                        │
│       1247 ppm         │  ← Big CO2 reading
│                        │
│      24.5C  45%        │  ← Temp and humidity
├────────────────────────┤
│ WiFi OK  Buf:3/15  2h  │  ← Status bar
└────────────────────────┘
```

**Status bar shows:**
- WiFi connection state
- Buffer count (readings waiting to upload)
- Uptime

## Features

**Local display:** Real-time CO2, temperature, and humidity on OLED. Updates after each 60-second measurement.

**Batch uploading:** Readings buffered locally (up to 15) with ISO 8601 timestamps. Batches sent every 10 minutes.

**Visual feedback during startup:**
- WiFi connection progress bar
- Time sync status
- Sensor initialization

**Error display:** Shows I2C errors and recovery attempts on screen.

**Forced Recalibration (FRC):** Hold BOOT button for 3 seconds. Display shows calibration progress.

## Files

| File | Purpose |
|------|---------|
| `scd41-co2-monitor-oled.ino` | Main code with OLED integration |
| `secrets.h` | WiFi creds and API token (gitignored) |
| `forced_calibration.h` | FRC module triggered by BOOT button |

## Notes

- OLED requires 5V on VCC (3.3V may not work)
- OLED uses software SPI (any GPIO pins work)
- SCD41 uses I2C on dedicated pins (21/22)
- No pin conflicts between sensor and display
