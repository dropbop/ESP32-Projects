# Talk to RPi

ESP32 sketch for testing HTTPS POST requests to an API endpoint (e.g., on a Raspberry Pi).

## What it does

- Connects to WiFi
- Generates a unique device ID from the ESP32's MAC address
- Sends JSON payloads via HTTPS POST every second
- Reports response time and status

## Setup

Copy `secrets.h.example` to `secrets.h` and fill in:
- `WIFI_SSID` / `WIFI_PASS` - your WiFi credentials
- `API_URL` - the endpoint to POST to
- `API_KEY` - API key sent in `X-API-Key` header

## Notes

- Uses `setInsecure()` to skip certificate verification (fine for local testing)
- Keeps connection alive with `setReuse(true)` for lower latency
