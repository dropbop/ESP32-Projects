#include <LiquidCrystal.h>

// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(13, 12, 14, 27, 26, 25);

void setup() {
  delay(500);  // let LCD power up
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Test");
}

void loop() {
  lcd.setCursor(0, 1);
  lcd.print(millis() / 1000);
}