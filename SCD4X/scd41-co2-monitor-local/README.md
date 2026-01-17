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
const char* apiEndpoint = "http://192.168.1.100:5001";  // Your server IP
```

## Required Libraries

Install via Arduino Library Manager:
- `Sensirion I2C SCD4x` by Sensirion

## Behavior

- Takes a reading every 30 seconds
- Sends each reading immediately to the API
- Auto-reconnects WiFi if disconnected
- I2C bus recovery after 3 consecutive sensor errors
- Blue LED: 1 flash on success, 3 flashes on failure

## API Endpoint

`POST /api/sensor`

```json
{"device":"office","co2":650,"temp":18.1,"humidity":62.5}
```

## Multi-Sensor Setup

Deploy multiple sensors by flashing with different `deviceName` values:
- `"office"`
- `"bedroom"`
- `"garage"`

All data goes to the same database, distinguished by the `device` column.
