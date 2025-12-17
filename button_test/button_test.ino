#define BUTTON_PIN 0  // GPIO0 - built-in button on most ESP32 dev boards

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    Serial.println();
    Serial.println("Button test ready - press GPIO0 button");
}

void loop() {
    if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println("Button pressed!");
        delay(200);  // simple debounce - wait before checking again
    }
}
