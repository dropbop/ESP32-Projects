# MQTT Example: Fan Control Coordination

Example of using MQTT for real-time ESP32-to-ESP32 communication.

## Use Case

The SCD41 CO2 monitor needs to turn on a fan via IR blaster when CO2 is high. But the fan beeps loudly if it receives repeated IR signals. A second ESP32 near the fan detects when the fan is actually on and tells the SCD41 to stop blasting IR.

```
┌─────────────────────┐                      ┌─────────────────────┐
│   SCD41-ESP32       │                      │   Fan-ESP32         │
│   (CO2 sensor + IR) │                      │   (fan state detect)│
└─────────┬───────────┘                      └──────────┬──────────┘
          │                                             │
          │ 1. CO2 high → blast IR                      │
          │                                             │
          ├──publish─► "fan/blasting" ──────────────────┤
          │                                   subscribe │
          │                                             │
          │                              2. Detects fan │
          │                                 is now on   │
          │                                             │
          ├────────────── "fan/status: on" ◄──publish───┤
subscribe │                                             │
          │                                             │
          │ 3. Stop IR blasting                         │
          │                                             │
          ▼                                             ▼
```

## MQTT Broker

Mosquitto runs on the ThinkPad (192.168.1.112 / 100.77.157.78).

```bash
# Test it works
mosquitto_sub -t "fan/#" -v      # Terminal 1
mosquitto_pub -t "fan/test" -m "hello"  # Terminal 2
```

## Topics

| Topic | Publisher | Subscriber | Payload |
|-------|-----------|------------|---------|
| `fan/blasting` | SCD41-ESP32 | Fan-ESP32 | `1` = blasting, `0` = stopped |
| `fan/status` | Fan-ESP32 | SCD41-ESP32 | `on` / `off` |

## Arduino Library

Install **PubSubClient** by Nick O'Leary via Library Manager.

## Example Code: SCD41-ESP32 (Publisher + Subscriber)

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
const char* mqtt_server = "192.168.1.112";

WiFiClient espClient;
PubSubClient client(espClient);

bool fanConfirmedOn = false;

void callback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }

    if (String(topic) == "fan/status") {
        if (msg == "on") {
            fanConfirmedOn = true;
            Serial.println("Fan confirmed ON - stopping IR");
        } else if (msg == "off") {
            fanConfirmedOn = false;
        }
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("MQTT connecting...");
        if (client.connect("SCD41-ESP32")) {
            Serial.println("connected");
            client.subscribe("fan/status");
        } else {
            Serial.print("failed, rc=");
            Serial.println(client.state());
            delay(2000);
        }
    }
}

void setup() {
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    // Example: when CO2 is high and fan not confirmed on
    bool co2High = false;  // Replace with actual CO2 check

    if (co2High && !fanConfirmedOn) {
        // Blast IR here
        // irSendFanOn();

        // Tell fan-ESP32 we're blasting
        client.publish("fan/blasting", "1");
        Serial.println("Blasting IR, published fan/blasting=1");

        delay(5000);  // Wait before retry
    }
}
```

## Example Code: Fan-ESP32 (Subscriber + Publisher)

```cpp
#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
const char* mqtt_server = "192.168.1.112";

WiFiClient espClient;
PubSubClient client(espClient);

// Pin for fan detection (current sensor, vibration, etc.)
#define FAN_DETECT_PIN 34

void callback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }

    if (String(topic) == "fan/blasting" && msg == "1") {
        Serial.println("SCD41 is blasting IR - checking fan state...");
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("MQTT connecting...");
        if (client.connect("Fan-ESP32")) {
            Serial.println("connected");
            client.subscribe("fan/blasting");
        } else {
            Serial.print("failed, rc=");
            Serial.println(client.state());
            delay(2000);
        }
    }
}

bool isFanOn() {
    // Implement your detection logic
    // Could be: current sensor, vibration sensor, sound, etc.
    int reading = analogRead(FAN_DETECT_PIN);
    return reading > 500;  // Adjust threshold
}

void setup() {
    Serial.begin(115200);
    pinMode(FAN_DETECT_PIN, INPUT);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

bool lastFanState = false;

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    bool fanOn = isFanOn();

    // Publish on state change
    if (fanOn != lastFanState) {
        if (fanOn) {
            client.publish("fan/status", "on");
            Serial.println("Fan ON - published fan/status=on");
        } else {
            client.publish("fan/status", "off");
            Serial.println("Fan OFF - published fan/status=off");
        }
        lastFanState = fanOn;
    }

    delay(100);
}
```

## Testing Without Hardware

Use mosquitto CLI to simulate:

```bash
# Simulate SCD41 blasting
mosquitto_pub -t "fan/blasting" -m "1"

# Simulate fan confirming it's on
mosquitto_pub -t "fan/status" -m "on"

# Watch all fan topics
mosquitto_sub -t "fan/#" -v
```

## Notes

- MQTT runs on port 1883 (no auth for LAN)
- Keep `client.loop()` in your main loop for responsive callbacks
- Use retained messages (`-r` flag) if you want new subscribers to get last state
- This is independent of the HTTP sensor→PostgreSQL pipeline
