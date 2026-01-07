#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "secrets.h"

WiFiClientSecure client;
HTTPClient http;

String deviceId;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Generate random device ID
  uint32_t chip = ESP.getEfuseMac() & 0xFFFFFF;
  deviceId = "esp32-" + String(chip, HEX);
  Serial.println("Device ID: " + deviceId);
  
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected: " + WiFi.localIP().toString());
  
  // Skip cert verification (fine for testing, revisit for prod)
  client.setInsecure();
}

String randomString(int len) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  String result = "";
  for (int i = 0; i < len; i++) {
    result += charset[random(0, sizeof(charset) - 1)];
  }
  return result;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  unsigned long start = millis();
  
  // Build JSON payload
  String timestamp = "2025-01-07T12:00:00Z"; // Fake timestamp for testing
  String fieldA = randomString(8);
  String fieldB = randomString(8);
  
  String payload = "{\"readings\":[{";
  payload += "\"device_id\":\"" + deviceId + "\",";
  payload += "\"ts\":\"" + timestamp + "\",";
  payload += "\"field_a\":\"" + fieldA + "\",";
  payload += "\"field_b\":\"" + fieldB + "\"";
  payload += "}]}";

  http.begin(client, API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  http.setReuse(true); // Keep-alive
  
  int httpCode = http.POST(payload);
  String response = http.getString();
  
  unsigned long elapsed = millis() - start;
  
  if (httpCode == 200) {
    Serial.printf("[OK] %dms - %s\n", elapsed, response.c_str());
  } else {
    Serial.printf("[ERR] %d - %dms - %s\n", httpCode, elapsed, response.c_str());
  }
  
  http.end();
  
  delay(1000);
}