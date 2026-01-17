# Local Sensor API

Flask API server that receives ESP32 sensor data and stores it in PostgreSQL.

No authentication - intended for LAN-only access.

## Setup

### 1. Database Setup

Run as the postgres user:

```bash
sudo -u postgres psql -f setup_db.sql
```

Or manually:

```sql
CREATE DATABASE sensor_data;
\c sensor_data

CREATE TABLE readings (
    id SERIAL PRIMARY KEY,
    device VARCHAR(50) NOT NULL,
    co2 INTEGER,
    temp REAL,
    humidity REAL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE sensor_events (
    id SERIAL PRIMARY KEY,
    device VARCHAR(50) NOT NULL,
    event_type VARCHAR(20) DEFAULT 'info',
    message TEXT,
    uptime_seconds INTEGER,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_readings_device ON readings(device);
CREATE INDEX idx_readings_created_at ON readings(created_at);

GRANT ALL PRIVILEGES ON DATABASE sensor_data TO your_user;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO your_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO your_user;
```

### 2. Python Environment

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### 3. Configuration

```bash
cp .env.example .env
# Edit .env with your database URL if needed
```

Default: `postgresql://localhost/sensor_data`

### 4. Run

```bash
source venv/bin/activate
python sensor_api.py
```

Server runs on `http://0.0.0.0:5001`

## Systemd Service (Production)

For always-on deployment (home server, Raspberry Pi, etc.), use the included systemd service.

The service file is already configured. Install it:

```bash
sudo cp sensor-api.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now sensor-api
```

Check status and logs:

```bash
sudo systemctl status sensor-api
journalctl -u sensor-api -f
```

Edit `sensor-api.service` if your paths differ from the defaults:
- Working directory: `/home/dropbop/code/ESP32-Projects/SCD4X/scd41-co2-monitor-local/local-sensor-api`
- Python venv: `venv/bin/python`

## Endpoints

### Health Check

```bash
curl http://localhost:5001/health
```

### Single Reading

```bash
curl -X POST http://localhost:5001/api/sensor \
  -H "Content-Type: application/json" \
  -d '{"device": "office", "co2": 800, "temp": 22.0, "humidity": 45.0}'
```

### Batch Readings

```bash
curl -X POST http://localhost:5001/api/sensor/batch \
  -H "Content-Type: application/json" \
  -d '{
    "device": "office",
    "readings": [
      {"co2": 800, "temp": 22.0, "humidity": 45.0, "ts": "2026-01-16T12:00:00Z"},
      {"co2": 820, "temp": 22.1, "humidity": 45.2, "ts": "2026-01-16T12:01:00Z"}
    ]
  }'
```

### Event Logging

```bash
curl -X POST http://localhost:5001/api/sensor/log \
  -H "Content-Type: application/json" \
  -d '{"device": "office", "event_type": "info", "message": "Sensor started", "uptime": 0}'
```

## Query Data

```bash
# Recent readings
psql sensor_data -c "SELECT * FROM readings ORDER BY created_at DESC LIMIT 10;"

# Readings by device
psql sensor_data -c "SELECT * FROM readings WHERE device='office' ORDER BY created_at DESC LIMIT 10;"

# Hourly averages
psql sensor_data -c "
  SELECT
    date_trunc('hour', created_at) as hour,
    device,
    ROUND(AVG(co2)) as avg_co2,
    ROUND(AVG(temp)::numeric, 1) as avg_temp
  FROM readings
  WHERE created_at > NOW() - INTERVAL '24 hours'
  GROUP BY hour, device
  ORDER BY hour DESC;
"

# Events
psql sensor_data -c "SELECT * FROM sensor_events ORDER BY created_at DESC LIMIT 10;"
```
