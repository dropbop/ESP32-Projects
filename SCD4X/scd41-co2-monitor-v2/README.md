# SCD41 CO2 Monitor

ESP32-based CO2/temperature/humidity monitor using the Sensirion SCD41 sensor. Sends readings every 60 seconds to a local Flask API.

## Features

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

### Wiring

| SCD41 Pin | ESP32 Pin | Wire Color (typical) |
|-----------|-----------|----------------------|
| VDD       | 3V3       | Red                  |
| GND       | GND       | Black                |
| SDA       | GPIO 21   | Green                |
| SCL       | GPIO 22   | Yellow               |

Built-in LED on GPIO 2 provides status feedback.

## Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support
2. Install library: **Sensirion I2C SCD4x** (Library Manager)
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
| `scd41-co2-monitor-v2.ino` | Main code |
| `secrets.h` | WiFi and API config (gitignored) |
| `secrets.h.example` | Template for secrets.h |
| `forced_calibration.h` | Manual calibration module |

## Calibration

### Automatic Self-Calibration (ASC)

ASC is **disabled** in this build. The sensor relies on manual Forced Recalibration (FRC) instead. This gives you direct control over when and how calibration happens, rather than relying on the sensor's algorithm to detect "clean air" conditions.

If you prefer automatic calibration, you can enable ASC by changing line ~428 in the .ino file to `sensor.setAutomaticSelfCalibrationEnabled(true)`. ASC requires the sensor to see fresh outdoor air (~420-440 ppm) for at least 3 minutes, at least once per week.

### Forced Recalibration (FRC)

The primary calibration method:

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

### Sensor not found at startup
- Check wiring (SDA→21, SCL→22, VDD→3V3, GND→GND)
- Ensure sensor has good solder joints
- Try power cycling

### Garbage characters in serial monitor
- Set baud rate to 115200
- Check that no other code is writing to serial

### WiFi keeps disconnecting
- Check signal strength (RSSI in readings should be > -80 dBm)
- Move ESP32 closer to router or add external antenna

### CO2 readings seem wrong
- Perform FRC calibration outside
- Ensure ASC has opportunity to work (outdoor exposure weekly)
- Check altitude setting matches your location

### I2C errors
- Code attempts automatic recovery after 3 consecutive failures
- If persistent, check wiring and try shorter I2C wires
- Add 10kΩ pull-up resistors on SDA/SCL if not present

## Technical Notes

### Why Periodic Mode?

This code uses **periodic measurement mode** (not single-shot with power-down) because:

1. **FRC requires it** - Forced Recalibration needs the sensor to be warmed up and measuring
2. **Simpler code** - No wake/sleep dance needed
3. **Consistent timing** - Sensor updates every 5 seconds internally

Trade-off: ~15mA average current vs ~0.5mA with single-shot. For mains-powered applications, this doesn't matter.

Note: If you enable ASC, periodic mode is required for it to function.

### Measurement Timing

- Sensor updates internally every 5 seconds
- Code reads every 60 seconds (configurable)
- 60s matches the sensor's τ63% response time for CO2
- Faster polling just gets intermediate settling values

### Altitude Compensation

CO2 readings are affected by air pressure. The sensor compensates using either:
- **Altitude** (static, set once) - used in this code
- **Ambient pressure** (dynamic, from a barometer) - better if you have a BME280

Formula: ~0.1% error per 100m altitude error
