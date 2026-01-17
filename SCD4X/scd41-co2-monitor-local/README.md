# SCD41 CO2 Monitor - Local Stack

Complete local CO2 monitoring solution: ESP32 sensor firmware + Flask API + PostgreSQL storage.

No cloud dependencies, no authentication - designed for LAN-only home/office deployment.

## Components

```
scd41-co2-monitor-local/
├── scd41-co2-monitor-local.ino   # ESP32 firmware
├── secrets.h.example              # WiFi/endpoint config template
└── local-sensor-api/              # Flask server
    ├── sensor_api.py              # API server
    ├── setup_db.sql               # PostgreSQL schema
    └── sensor-api.service         # Systemd service file
```

## Quick Start

### 1. Server Setup (Raspberry Pi, Linux box, etc.)

```bash
cd local-sensor-api
sudo -u postgres psql -f setup_db.sql
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python sensor_api.py
```

Server runs on `http://0.0.0.0:5001`

See [local-sensor-api/README.md](local-sensor-api/README.md) for systemd setup and detailed configuration.

### 2. ESP32 Setup

1. Copy `secrets.h.example` to `secrets.h`
2. Edit with your WiFi credentials and server IP:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const char* deviceName = "office";
   const char* apiEndpoint = "http://192.168.1.100:5001";
   ```
3. Install library: **Sensirion I2C SCD4x** (Arduino Library Manager)
4. Flash to ESP32

### 3. Wiring

| SCD41 | ESP32 |
|-------|-------|
| VDD   | 3V3   |
| GND   | GND   |
| SDA   | GPIO 21 |
| SCL   | GPIO 22 |

## Behavior

- ESP32 takes a reading every 30 seconds
- Each reading POSTs immediately to `/api/sensor`
- Auto-reconnects WiFi if disconnected
- Blue LED: 1 flash = success, 3 flashes = failure

## Multi-Sensor Setup

Flash multiple ESP32s with different `deviceName` values:
- `"office"`, `"bedroom"`, `"garage"`, etc.

All data stored in same database, distinguished by `device` column.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/sensor` | POST | Single reading |
| `/api/sensor/batch` | POST | Batch readings |
| `/api/sensor/log` | POST | Event logging |

## Query Data

```bash
psql sensor_data -c "SELECT * FROM readings ORDER BY created_at DESC LIMIT 10;"
psql sensor_data -c "SELECT device, AVG(co2), AVG(temp) FROM readings GROUP BY device;"
```
