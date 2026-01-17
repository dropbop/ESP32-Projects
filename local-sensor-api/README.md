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

GRANT ALL PRIVILEGES ON DATABASE sensor_data TO dropbop;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO dropbop;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO dropbop;
```

### 2. Python Environment

```bash
cd /home/dropbop/code/ESP32-Projects/local-sensor-api
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

### 3. Configuration

```bash
cp .env.example .env
# Edit .env if needed (defaults work for local PostgreSQL)
```

### 4. Run

```bash
source venv/bin/activate
python sensor_api.py
```

Server runs on `http://0.0.0.0:5000`

## Systemd Service (Auto-start)

```bash
sudo cp sensor-api.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable sensor-api
sudo systemctl start sensor-api

# Check status
sudo systemctl status sensor-api
```

## Endpoints

### Health Check

```bash
curl http://localhost:5000/health
```

### Receive Batch

```bash
curl -X POST http://localhost:5000/api/sensor/batch \
  -H "Content-Type: application/json" \
  -d '{
    "device": "test",
    "readings": [
      {"co2": 800, "temp": 22.0, "humidity": 45.0, "ts": "2026-01-16T12:00:00Z"}
    ]
  }'
```

### Receive Event

```bash
curl -X POST http://localhost:5000/api/sensor/log \
  -H "Content-Type: application/json" \
  -d '{
    "device": "test",
    "event_type": "info",
    "message": "Test event",
    "uptime": 12345
  }'
```

## Query Data

```bash
# Recent readings
psql sensor_data -c "SELECT * FROM readings ORDER BY created_at DESC LIMIT 10;"

# Readings by device
psql sensor_data -c "SELECT * FROM readings WHERE device='office' ORDER BY created_at DESC LIMIT 10;"

# Recent events
psql sensor_data -c "SELECT * FROM sensor_events ORDER BY created_at DESC LIMIT 10;"
```
