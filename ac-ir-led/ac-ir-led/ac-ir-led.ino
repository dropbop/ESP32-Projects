void setup() {
    Serial.begin(115200);
    pinMode(2, OUTPUT);
    Serial.println("Blinking GPIO2...");
}

void loop() {
    digitalWrite(2, HIGH);
    Serial.println("HIGH");
    delay(500);
    digitalWrite(2, LOW);
    Serial.println("LOW");
    delay(500);
}