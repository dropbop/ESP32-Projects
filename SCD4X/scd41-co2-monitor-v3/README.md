# SCD41 CO2 Monitor v3

ESP32-based CO2/temperature/humidity monitor with OLED display and IR blaster. Combines the Sensirion SCD41 sensor, a 1.3" OLED for local readout, and IR control for a Whynter AC unit. Sends readings every 60 seconds to a local Flask API.

## Features

- **OLED display** - Real-time CO2, temperature, humidity, and status
- **IR blaster** - Control Whynter AC via serial commands
- **Periodic measurement mode** with ASC disabled (uses manual FRC calibration)
- **60-second measurement interval** - matches sensor response time
- **Altitude compensation** - configured for Houston, TX (15m)
- **Temperature offset compensation** - currently set to 3.6°C
- **Forced recalibration (FRC)** via BOOT button for manual calibration
- **I2C bus recovery** - automatic recovery from stuck I2C transactions
- **Watchdog timer** - automatic ESP32 reset if code hangs
- **Event logging** - errors, calibration events, and health reports sent to API

## Hardware

- ESP32 Dev Module (CP2102 or similar)
- [Sensirion SCD41](https://sensirion.com/products/catalog/SCD41) CO2 sensor
- [Inland 1.3" 128x64 OLED](https://www.microcenter.com/product/643965) (SH1106 driver, SPI)
- IR LED + 100Ω resistor

### Wiring

#### SCD41 (I2C)

| SCD41 Pin | ESP32 Pin | Wire Color (typical) |
|-----------|-----------|----------------------|
| VDD       | 3V3       | Red                  |
| GND       | GND       | Black                |
| SDA       | GPIO 21   | Green                |
| SCL       | GPIO 22   | Yellow               |

#### OLED Display (SPI)

| OLED Pin | ESP32 Pin | Notes |
|----------|-----------|-------|
| VCC      | VIN (5V)  | Must be 5V, not 3.3V |
| GND      | GND       | |
| CLK      | GPIO 25   | SPI Clock |
| MOSI     | GPIO 26   | SPI Data |
| RES      | GPIO 12   | Reset |
| DC       | GPIO 14   | Data/Command |
| CS       | GPIO 27   | Chip Select |

**Note:** The display is configured for upside-down mounting (180° rotation). If your display is right-side up, change `U8G2_R2` to `U8G2_R0` in the code.

#### IR LED

| Component | ESP32 Pin |
|-----------|-----------|
| IR LED Anode | GPIO 4 (through 100Ω resistor) |
| IR LED Cathode | GND |

Built-in LED on GPIO 2 provides status feedback.

## Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support
2. Install libraries via Library Manager:
   - **Sensirion I2C SCD4x**
   - **U8g2**
   - **IRremoteESP8266**
3. Copy `secrets.h.example` to `secrets.h` and configure:
   ```cpp
   const char* ssid = "your_wifi";
   const char* password = "your_password";
   const char* apiEndpoint = "http://192.168.1.xxx:5001";
   const char* deviceName = "office";
   ```
4. Adjust configuration in main `.ino` if needed:
   - `SENSOR_ALTITUDE_METERS` - your elevation (default: 15m for Houston)
   - `TEMPERATURE_OFFSET_C` - calibrate against a reference thermometer
5. Upload to ESP32

## Files

| File | Purpose |
|------|---------|
| `scd41-co2-monitor-v3.ino` | Main code |
| `secrets.h` | WiFi and API config (gitignored) |
| `secrets.h.example` | Template for secrets.h |
| `forced_calibration.h` | Manual calibration module |

## OLED Display

The display shows:

```
     +-----------------+
     |      850        |  CO2 (large)
     |            ppm  |
     |   22.5C  45%    |  Temp & Humidity
     +-----------------+
     |WiFi -65  IR:ON 5m|  Status bar
     +-----------------+
```

**Status bar contents:**
- WiFi signal strength (RSSI in dBm)
- IR spam status (if active)
- Uptime

**Display states:**
- `---` - Waiting for first reading
- `ERR` - Sensor communication error
- Number - Current CO2 in ppm

## Serial Commands

Control the IR blaster via Arduino Serial Monitor (115200 baud):

| Command | Action |
|---------|--------|
| `on`    | Send AC ON signal once |
| `off`   | Send AC OFF signal once |
| `spam`  | Toggle continuous IR sending |
| `spamon`| Start spamming ON signal (250ms interval) |
| `spamoff`| Start spamming OFF signal (250ms interval) |
| `stop`  | Stop spamming |
| `help`  | Print available commands |

## Calibration

### Automatic Self-Calibration (ASC)

ASC is **disabled** in this build. The sensor relies on manual Forced Recalibration (FRC) instead.

### Forced Recalibration (FRC)

1. Take sensor outside to fresh air (away from roads/HVAC exhaust)
2. Power on and wait for WiFi connection
3. **Hold BOOT button for 3 seconds**
4. Wait 5 minutes (LED blinks for each warmup reading)
5. Calibration completes automatically

The reference CO2 is set to **440 ppm** (appropriate for Houston urban area). Adjust `FRC_REFERENCE_PPM` in `forced_calibration.h` if needed:
- Rural/remote areas: 420 ppm
- Suburban: 430-440 ppm
- Urban near traffic: 450+ ppm

### Temperature Offset

The sensor generates heat during operation. Current offset is 3.6°C. To calibrate:

1. Let sensor run for 30+ minutes to reach thermal equilibrium
2. Compare sensor temperature to a reference thermometer
3. Calculate: `new_offset = sensor_reading - reference + current_offset`
4. Update `TEMPERATURE_OFFSET_C` in the code

## LED Feedback

| Pattern | Meaning |
|---------|---------|
| 1 flash | Reading sent successfully |
| 3 flashes | Upload failed |
| Slow blink | Connecting to WiFi |
| 5 quick flashes | FRC acknowledged, starting warmup |
| 2 slow flashes | FRC completed successfully |
| Rapid flashing | FRC failed |

## API Endpoints

### Sensor Reading (`POST /api/sensor`)

```json
{
  "device": "office",
  "co2": 850,
  "temp": 22.1,
  "humidity": 45.3,
  "rssi": -65,
  "uptime": 3600,
  "heap": 200000
}
```

### Event Log (`POST /api/sensor/log`)

```json
{
  "device": "office",
  "event_type": "info",
  "message": "Sensor started, serial: XXXX",
  "uptime": 0,
  "heap": 250000,
  "total_measurements": 0,
  "i2c_errors": 0
}
```

Event types: `info`, `warning`, `error`, `critical`

## Troubleshooting

### OLED display is blank
- Check VCC is connected to 5V (VIN), not 3.3V
- Verify SPI wiring (CLK, MOSI, CS, DC, RES)
- Try different GPIO pins if there's a conflict

### OLED display is upside-down
- Change `U8G2_R2` to `U8G2_R0` in the display constructor

### Sensor not found at startup
- Check I2C wiring (SDA→21, SCL→22, VDD→3V3, GND→GND)
- Ensure sensor has good solder joints
- Try power cycling

### IR commands not working
- Verify IR LED is on GPIO 4 with correct polarity
- Check 100Ω resistor is in series with LED
- Point LED directly at AC unit's IR receiver
- Try `spamon` to continuously send signal while adjusting aim

### WiFi keeps disconnecting
- Check signal strength (RSSI in display status bar should be > -80 dBm)
- Move ESP32 closer to router or add external antenna

### CO2 readings seem wrong
- Perform FRC calibration outside
- Check altitude setting matches your location
- Allow 24+ hours for sensor to stabilize after first power-on

### I2C errors
- Code attempts automatic recovery after 3 consecutive failures
- If persistent, check wiring and try shorter I2C wires
- OLED uses SPI, so I2C errors are SCD41-specific

## Technical Notes

### Why Periodic Mode?

This code uses **periodic measurement mode** (not single-shot with power-down) because:

1. **FRC requires it** - Forced Recalibration needs the sensor to be warmed up and measuring
2. **Simpler code** - No wake/sleep dance needed
3. **Consistent timing** - Sensor updates every 5 seconds internally

Trade-off: ~15mA average current vs ~0.5mA with single-shot. For mains-powered applications, this doesn't matter.

### Bus Sharing

- SCD41 uses **I2C** (GPIO 21/22)
- OLED uses **SPI** (GPIO 25/26/27/14/12)
- No bus conflicts between components

### Memory Usage

The U8g2 library uses a full frame buffer (~1KB for 128x64 display). With WiFi, HTTP, IR, and sensor libraries, expect ~180KB free heap at runtime.
