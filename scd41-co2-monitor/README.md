# ESP32 SCD41 CO2 Sensor

ESP32-based environmental monitor using a Sensirion SCD41 CO2/temperature/humidity sensor. Posts data to a remote API for logging and visualization.

## Hardware

- ESP32 Dev Module (CP2102)
- [Sensirion SCD41 CO2 Sensor](https://sensirion.com/products/catalog/SCD41) - see product page for datasheet and programming best practices

### Wiring

| SCD41 | ESP32 |
|-------|-------|
| GND (Black) | GND |
| VDD (Red) | 3V3 |
| SDA (Green) | GPIO 21 |
| SCL (Yellow) | GPIO 22 |

## Setup

1. Install Arduino IDE and ESP32 board support
2. Install library: `Sensirion I2C SCD4x` via Library Manager
3. Copy `sensor/secrets.h.example` to `sensor/secrets.h`
4. Edit `secrets.h` with your WiFi credentials and API token
5. Open `sensor/sensor.ino` in Arduino IDE
6. Select board: "ESP32 Dev Module"
7. Upload

## Configuration

Edit `secrets.h`:

```cpp
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";
const char* sensorToken = "your_api_token";
```

Edit `sensor.ino` for device name and endpoint:

```cpp
const char* endpoint = "https://your-domain.com/api/sensor";
const char* deviceName = "office";
```

## Data Format

Posts JSON every 20 seconds:

```json
{
  "device": "office",
  "co2": 1402,
  "temp": 18.6,
  "humidity": 69.2
}
```
