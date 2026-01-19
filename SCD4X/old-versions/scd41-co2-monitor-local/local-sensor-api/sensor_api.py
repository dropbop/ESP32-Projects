#!/usr/bin/env python3
"""
Local Sensor API - Receives ESP32 sensor data and stores in PostgreSQL.

No authentication - intended for LAN-only access.
"""

import os
from datetime import datetime, timezone, timedelta
from flask import Flask, request, jsonify
from flask_cors import CORS
from dotenv import load_dotenv
import psycopg

load_dotenv()

app = Flask(__name__)
CORS(app)  # Enable CORS for all routes

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


# =============================================================================
# GET Endpoints (for reading data)
# =============================================================================

@app.route('/api/sensor', methods=['GET'])
def get_readings():
    """
    Get sensor readings.

    Query params:
    - device: device name (required)
    - hours: number of hours of data to fetch (default: 24)
    """
    device = request.args.get('device')
    if not device:
        return jsonify({'error': 'Missing device parameter'}), 400

    hours = int(request.args.get('hours', 24))
    cutoff = datetime.now(timezone.utc) - timedelta(hours=hours)

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                cur.execute(
                    """
                    SELECT co2, temp, humidity, created_at
                    FROM readings
                    WHERE device = %s AND created_at >= %s
                    ORDER BY created_at ASC
                    """,
                    (device, cutoff)
                )
                rows = cur.fetchall()

        readings = [
            {
                'co2': row[0],
                'temp': float(row[1]) if row[1] is not None else None,
                'humidity': float(row[2]) if row[2] is not None else None,
                'ts': row[3].isoformat() if row[3] else None
            }
            for row in rows
        ]

        return jsonify(readings)

    except Exception as e:
        print(f"[ERROR] Get readings failed: {e}")
        return jsonify({'error': str(e)}), 500


@app.route('/api/sensor/log', methods=['GET'])
def get_events():
    """
    Get sensor events/log messages.

    Query params:
    - device: device name (required)
    - hours: number of hours of data to fetch (default: 24)
    - limit: max number of events to return (default: 50)
    - type: filter by event_type (optional)
    """
    device = request.args.get('device')
    if not device:
        return jsonify({'error': 'Missing device parameter'}), 400

    hours = int(request.args.get('hours', 24))
    limit = int(request.args.get('limit', 50))
    event_type = request.args.get('type')
    cutoff = datetime.now(timezone.utc) - timedelta(hours=hours)

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                if event_type:
                    cur.execute(
                        """
                        SELECT event_type, message, uptime_seconds, created_at
                        FROM sensor_events
                        WHERE device = %s AND created_at >= %s AND event_type = %s
                        ORDER BY created_at DESC
                        LIMIT %s
                        """,
                        (device, cutoff, event_type, limit)
                    )
                else:
                    cur.execute(
                        """
                        SELECT event_type, message, uptime_seconds, created_at
                        FROM sensor_events
                        WHERE device = %s AND created_at >= %s
                        ORDER BY created_at DESC
                        LIMIT %s
                        """,
                        (device, cutoff, limit)
                    )
                rows = cur.fetchall()

        events = [
            {
                'event_type': row[0],
                'message': row[1],
                'uptime': row[2],
                'heap': None,
                'total_measurements': None,
                'ts': row[3].isoformat() if row[3] else None
            }
            for row in rows
        ]

        return jsonify(events)

    except Exception as e:
        print(f"[ERROR] Get events failed: {e}")
        return jsonify({'error': str(e)}), 500


@app.route('/api/sensor/calibration', methods=['GET'])
def get_last_calibration():
    """
    Get last calibration date for a device.

    Query params:
    - device: device name (required)
    """
    device = request.args.get('device')
    if not device:
        return jsonify({'error': 'Missing device parameter'}), 400

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                # Look for calibration events (message contains "calibrat" case-insensitive)
                cur.execute(
                    """
                    SELECT created_at
                    FROM sensor_events
                    WHERE device = %s AND LOWER(message) LIKE %s
                    ORDER BY created_at DESC
                    LIMIT 1
                    """,
                    (device, '%calibrat%')
                )
                row = cur.fetchone()

        if row and row[0]:
            return jsonify({'date': row[0].isoformat()})
        else:
            return jsonify({'date': None})

    except Exception as e:
        print(f"[ERROR] Get calibration failed: {e}")
        return jsonify({'error': str(e)}), 500


@app.route('/api/sensor/stats', methods=['GET'])
def get_stats():
    """
    Get aggregated statistics for a device.

    Query params:
    - device: device name (required)
    - hours: number of hours to aggregate (default: 24)
    """
    device = request.args.get('device')
    if not device:
        return jsonify({'error': 'Missing device parameter'}), 400

    hours = int(request.args.get('hours', 24))
    cutoff = datetime.now(timezone.utc) - timedelta(hours=hours)

    try:
        with get_db() as conn:
            with conn.cursor() as cur:
                cur.execute(
                    """
                    SELECT
                        COUNT(*) as count,
                        AVG(co2) as avg_co2,
                        MIN(co2) as min_co2,
                        MAX(co2) as max_co2,
                        AVG(temp) as avg_temp,
                        MIN(temp) as min_temp,
                        MAX(temp) as max_temp,
                        AVG(humidity) as avg_humidity,
                        MIN(humidity) as min_humidity,
                        MAX(humidity) as max_humidity
                    FROM readings
                    WHERE device = %s AND created_at >= %s
                    """,
                    (device, cutoff)
                )
                row = cur.fetchone()

        if row and row[0] > 0:
            return jsonify({
                'count': row[0],
                'co2': {
                    'avg': float(row[1]) if row[1] else None,
                    'min': row[2],
                    'max': row[3]
                },
                'temp': {
                    'avg': float(row[4]) if row[4] else None,
                    'min': float(row[5]) if row[5] else None,
                    'max': float(row[6]) if row[6] else None
                },
                'humidity': {
                    'avg': float(row[7]) if row[7] else None,
                    'min': float(row[8]) if row[8] else None,
                    'max': float(row[9]) if row[9] else None
                }
            })
        else:
            return jsonify({'count': 0, 'co2': None, 'temp': None, 'humidity': None})

    except Exception as e:
        print(f"[ERROR] Get stats failed: {e}")
        return jsonify({'error': str(e)}), 500


if __name__ == '__main__':
    print("Starting Local Sensor API...")
    print(f"Database: {DATABASE_URL}")
    app.run(host='0.0.0.0', port=5001, debug=False)
