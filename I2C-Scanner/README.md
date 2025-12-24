# I2C Scanner

Simple I2C bus scanner for debugging sensor connections. Created to troubleshoot an SCD41 sensor that wasn't responding on a breadboard (loose connections - ended up needing to solder).

## Usage

Upload to ESP32, open Serial Monitor at 115200 baud. Scans every 5 seconds and prints addresses of any detected devices.

## Wiring

| ESP32 | I2C Device |
|-------|------------|
| GPIO 21 | SDA |
| GPIO 22 | SCL |
| 3V3 | VCC |
| GND | GND |
