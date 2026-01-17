# SCD41 CO2 Monitor - Local Version

Simplified ESP32 sensor firmware that sends data to a local Flask API over plain HTTP. No display, no IR, no authentication.

## Hardware

- ESP32 Dev Module
- Sensirion SCD41 CO2/Temperature/Humidity sensor

### Wiring

| SCD41 Pin | ESP32 Pin |
|-----------|-----------|
| VDD | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

## Setup

1. Copy `secrets.h.example` to `secrets.h`
2. Edit `secrets.h` with your WiFi credentials and device name
3. Flash to ESP32 using Arduino IDE

### secrets.h

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* deviceName = "office";  // Change for each sensor
const char* apiEndpoint = "http://100.77.157.78:5000";  // ThinkPad IP
```

## Required Libraries

Install via Arduino Library Manager:
- `Sensirion I2C SCD4x` by Sensirion

## Behavior

- Takes a reading every 60 seconds
- Buffers up to 15 readings
- Uploads batch every 10 minutes (or when buffer is nearly full)
- Sends events/errors to `/api/sensor/log`
- Blue LED flashes: 2x on successful upload, 3x on failure

## Endpoints Called

- `POST /api/sensor/batch` - Batched readings
- `POST /api/sensor/log` - Events/errors

## Multi-Sensor Setup

Deploy multiple sensors by flashing the same firmware with different `deviceName` values:
- `"office"`
- `"bedroom"`
- `"garage"`

All data goes to the same database, distinguished by the `device` column.
