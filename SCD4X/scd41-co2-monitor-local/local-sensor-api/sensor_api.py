#!/usr/bin/env python3
"""
Local Sensor API - Receives ESP32 sensor data and stores in PostgreSQL.

No authentication - intended for LAN-only access.
"""

import os
from datetime import datetime, timezone
from flask import Flask, request, jsonify
from dotenv import load_dotenv
import psycopg

load_dotenv()

app = Flask(__name__)

DATABASE_URL = os.getenv('DATABASE_URL', 'postgresql://dropbop@localhost/sensor_data')


def get_db():
    """Get database connection."""
    return psycopg.connect(DATABASE_URL)


@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint."""
    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                cur.execute('SELECT 1')
        return jsonify({'status': 'ok', 'database': 'connected'})
    except Exception as e:
        return jsonify({'status': 'error', 'database': str(e)}), 500


@app.route('/api/sensor', methods=['POST'])
def receive_single():
    """
    Receive single sensor reading.

    Expected JSON:
    {"device": "office", "co2": 800, "temp": 22.0, "humidity": 45.0}
    """
    data = request.get_json()

    if not data:
        return jsonify({'error': 'No JSON data'}), 400

    device = data.get('device')
    if not device:
        return jsonify({'error': 'Missing device name'}), 400

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                cur.execute(
                    """
                    INSERT INTO readings (device, co2, temp, humidity)
                    VALUES (%s, %s, %s, %s)
                    """,
                    (device, data.get('co2'), data.get('temp'), data.get('humidity'))
                )

        print(f"[SINGLE] {device}: co2={data.get('co2')} temp={data.get('temp')} humidity={data.get('humidity')}")
        return jsonify({'status': 'ok'})

    except Exception as e:
        print(f"[ERROR] Single insert failed: {e}")
        return jsonify({'error': str(e)}), 500


@app.route('/api/sensor/batch', methods=['POST'])
def receive_batch():
    """
    Receive batched sensor readings.

    Expected JSON:
    {
        "device": "office",
        "readings": [
            {"co2": 800, "temp": 22.0, "humidity": 45.0, "ts": "2026-01-16T12:00:00Z"},
            ...
        ]
    }
    """
    data = request.get_json()

    if not data:
        return jsonify({'error': 'No JSON data'}), 400

    device = data.get('device')
    readings = data.get('readings', [])

    if not device:
        return jsonify({'error': 'Missing device name'}), 400

    if not readings:
        return jsonify({'error': 'No readings provided'}), 400

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                # Prepare values for batch insert
                values = []
                for r in readings:
                    ts = r.get('ts')
                    if ts:
                        # Parse ISO timestamp
                        try:
                            created_at = datetime.fromisoformat(ts.replace('Z', '+00:00'))
                        except ValueError:
                            created_at = datetime.now(timezone.utc)
                    else:
                        created_at = datetime.now(timezone.utc)

                    values.append((
                        device,
                        r.get('co2'),
                        r.get('temp'),
                        r.get('humidity'),
                        created_at
                    ))

                # Batch insert using executemany
                cur.executemany(
                    """
                    INSERT INTO readings (device, co2, temp, humidity, created_at)
                    VALUES (%s, %s, %s, %s, %s)
                    """,
                    values
                )

        print(f"[BATCH] {device}: {len(readings)} readings stored")
        return jsonify({'status': 'ok', 'stored': len(readings)})

    except Exception as e:
        print(f"[ERROR] Batch insert failed: {e}")
        return jsonify({'error': str(e)}), 500


@app.route('/api/sensor/log', methods=['POST'])
def receive_event():
    """
    Receive sensor event/log message.

    Expected JSON:
    {
        "device": "office",
        "event_type": "info",  // info, warning, error, critical
        "message": "Sensor started",
        "uptime": 12345
    }
    """
    data = request.get_json()

    if not data:
        return jsonify({'error': 'No JSON data'}), 400

    device = data.get('device')
    if not device:
        return jsonify({'error': 'Missing device name'}), 400

    event_type = data.get('event_type', 'info')
    message = data.get('message', '')
    uptime = data.get('uptime')

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                cur.execute(
                    """
                    INSERT INTO sensor_events (device, event_type, message, uptime_seconds)
                    VALUES (%s, %s, %s, %s)
                    """,
                    (device, event_type, message, uptime)
                )

        print(f"[EVENT] {device} ({event_type}): {message}")
        return jsonify({'status': 'ok'})

    except Exception as e:
        print(f"[ERROR] Event insert failed: {e}")
        return jsonify({'error': str(e)}), 500


if __name__ == '__main__':
    print("Starting Local Sensor API...")
    print(f"Database: {DATABASE_URL}")
    app.run(host='0.0.0.0', port=5001, debug=False)
