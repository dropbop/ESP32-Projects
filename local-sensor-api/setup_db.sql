-- Run this as the postgres user:
-- sudo -u postgres psql -f setup_db.sql

-- Create database (ignore error if exists)
CREATE DATABASE sensor_data;

\c sensor_data

-- Create tables
CREATE TABLE IF NOT EXISTS readings (
    id SERIAL PRIMARY KEY,
    device VARCHAR(50) NOT NULL,
    co2 INTEGER,
    temp REAL,
    humidity REAL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS sensor_events (
    id SERIAL PRIMARY KEY,
    device VARCHAR(50) NOT NULL,
    event_type VARCHAR(20) DEFAULT 'info',
    message TEXT,
    uptime_seconds INTEGER,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Create indexes for common queries
CREATE INDEX IF NOT EXISTS idx_readings_device ON readings(device);
CREATE INDEX IF NOT EXISTS idx_readings_created_at ON readings(created_at);
CREATE INDEX IF NOT EXISTS idx_events_device ON sensor_events(device);
CREATE INDEX IF NOT EXISTS idx_events_created_at ON sensor_events(created_at);

-- Grant permissions to dropbop user
GRANT ALL PRIVILEGES ON DATABASE sensor_data TO dropbop;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO dropbop;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO dropbop;
