/*
 * ============================================================================
 * Inland 1.3" 128x64 OLED Display Test Suite
 * ============================================================================
 * 
 * DISPLAY: Inland IIC/SPI 1.3" 128x64 OLED V2.0
 *          https://www.microcenter.com/product/643965/inland-iic-spi-13-128x64-oled-v20-graphic-display-module-for-arduino-uno-r3
 *          Driver: SH1106 (NOT SSD1306)
 *          
 * LIBRARY: U8g2 (install via Library Manager, include as <U8g2lib.h>)
 * 
 * VOLTAGE: 5V on VCC (3.3V may not work despite some claims)
 *          Signal lines are 3.3V logic (ESP32 native)
 * 
 * WIRING (Software SPI - confirmed working):
 *   OLED    ESP32
 *   ----    -----
 *   GND     GND
 *   VCC     VIN (5V)
 *   CLK     GPIO 25
 *   MOSI    GPIO 26
 *   RES     GPIO 12
 *   DC      GPIO 14
 *   CS      GPIO 27
 * 
 * ALTERNATIVE (Hardware SPI - faster, use if SW works):
 *   CLK     GPIO 18 (VSPI SCK)
 *   MOSI    GPIO 23 (VSPI MOSI)
 *   RES     GPIO 4  (avoid GPIO 19, it's VSPI MISO)
 *   DC      GPIO 21
 *   CS      GPIO 22
 * 
 * NOTES:
 *   - If display is blank, try U8G2_SSD1306 constructor (some 1.3" are mislabeled)
 *   - Software SPI works on any GPIO pins
 *   - Hardware SPI is faster but pins are fixed (except CS/DC/RES)
 *   - u8g2.begin() doesn't verify display is connected; it just sends init blindly
 * 
 * ============================================================================
 */

#include <Arduino.h>
#include <U8g2lib.h>

// =========================
// PIN DEFINITIONS
// =========================
#define PIN_CLK   25
#define PIN_MOSI  26
#define PIN_CS    27
#define PIN_DC    14
#define PIN_RES   12
#define PIN_LED   2   // Onboard LED (most ESP32 dev boards)

// =========================
// DISPLAY CONSTRUCTOR
// =========================
// Software SPI (flexible pins, slightly slower)
U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, PIN_CLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RES);

// Hardware SPI alternative (uncomment to use, rewire accordingly):
// U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI u8g2(U8G2_R0, /*cs=*/ 22, /*dc=*/ 21, /*reset=*/ 4);

// If display doesn't work, try SSD1306 driver:
// U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, PIN_CLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RES);

// =========================
// TEST PARAMETERS
// =========================
#define TEST_DURATION_MS 2500
#define NUM_TESTS 7

int currentTest = 0;
unsigned long lastSwitch = 0;
int frameCount = 0;

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println();
  Serial.println("================================");
  Serial.println("Inland OLED Display Test Suite");
  Serial.println("================================");
  Serial.printf("CLK:  GPIO %d\n", PIN_CLK);
  Serial.printf("MOSI: GPIO %d\n", PIN_MOSI);
  Serial.printf("CS:   GPIO %d\n", PIN_CS);
  Serial.printf("DC:   GPIO %d\n", PIN_DC);
  Serial.printf("RES:  GPIO %d\n", PIN_RES);
  Serial.println("VCC:  5V (VIN)");
  Serial.println("================================");
  
  pinMode(PIN_LED, OUTPUT);
  
  Serial.println("Initializing display...");
  u8g2.begin();
  Serial.println("Display initialized (no ACK available)");
  Serial.println("Starting test cycle...\n");
  
  lastSwitch = millis();
}

// =========================
// TEST SCREENS
// =========================

void testSplash() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(20, 25, "INLAND");
  u8g2.drawStr(28, 45, "1.3\" OLED");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(25, 60, "Test Suite");
  u8g2.sendBuffer();
}

void testTextSizes() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 8, "5x7: The quick brown fox");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 20, "6x10: Hello World");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 33, "ncenB08: Testing");
  u8g2.setFont(u8g2_font_ncenB12_tr);
  u8g2.drawStr(0, 50, "ncenB12: Big");
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(0, 64, "ncenB14");
  u8g2.sendBuffer();
}

void testShapes() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 7, "Shapes:");
  
  // Rectangle
  u8g2.drawFrame(5, 12, 30, 20);
  u8g2.drawBox(10, 17, 10, 10);
  
  // Circle
  u8g2.drawCircle(55, 22, 10);
  u8g2.drawDisc(55, 22, 5);
  
  // Triangle
  u8g2.drawTriangle(85, 32, 95, 12, 105, 32);
  
  // Lines
  u8g2.drawLine(0, 40, 127, 40);
  u8g2.drawLine(0, 45, 127, 55);
  u8g2.drawLine(0, 55, 127, 45);
  
  // Rounded rect
  u8g2.drawRFrame(80, 45, 45, 18, 5);
  
  u8g2.sendBuffer();
}

void testAnimation() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(30, 12, "Animation");
  
  // Bouncing ball
  int x = 64 + sin(frameCount * 0.1) * 50;
  int y = 40 + cos(frameCount * 0.15) * 15;
  u8g2.drawDisc(x, y, 8);
  
  // Spinning line
  int x2 = 64 + cos(frameCount * 0.2) * 20;
  int y2 = 40 + sin(frameCount * 0.2) * 20;
  u8g2.drawLine(64, 40, x2, y2);
  
  // Progress bar
  int progress = (frameCount * 3) % 128;
  u8g2.drawFrame(0, 55, 128, 8);
  u8g2.drawBox(1, 56, progress, 6);
  
  u8g2.sendBuffer();
}

void testPixelPattern() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(35, 7, "Pixel Test");
  
  // Checkerboard
  for (int x = 0; x < 64; x += 2) {
    for (int y = 12; y < 44; y += 2) {
      if ((x + y) % 4 == 0) {
        u8g2.drawPixel(x, y);
      }
    }
  }
  
  // Gradient-ish dither
  for (int x = 64; x < 128; x++) {
    for (int y = 12; y < 44; y++) {
      if (random(100) < (x - 64)) {
        u8g2.drawPixel(x, y);
      }
    }
  }
  
  // Resolution reminder
  u8g2.drawStr(25, 55, "128 x 64 pixels");
  u8g2.sendBuffer();
}

void testInfo() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  
  u8g2.drawStr(0, 8,  "Driver: SH1106");
  u8g2.drawStr(0, 18, "Resolution: 128x64");
  u8g2.drawStr(0, 28, "Interface: SPI (SW)");
  u8g2.drawStr(0, 38, "VCC: 5V required");
  u8g2.drawStr(0, 48, "Logic: 3.3V (ESP32)");
  
  u8g2.drawFrame(0, 52, 128, 12);
  u8g2.drawStr(4, 62, "microcenter.com #643965");
  
  u8g2.sendBuffer();
}

void testContrast() {
  // Cycle contrast to show it works
  static int contrast = 0;
  contrast = (contrast + 5) % 256;
  u8g2.setContrast(contrast);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(15, 25, "Contrast Test");
  
  char buf[20];
  sprintf(buf, "Level: %d/255", contrast);
  u8g2.drawStr(25, 45, buf);
  
  u8g2.drawFrame(14, 50, 100, 10);
  u8g2.drawBox(14, 50, (contrast * 100) / 255, 10);
  
  u8g2.sendBuffer();
}

// =========================
// MAIN LOOP
// =========================
void loop() {
  unsigned long now = millis();
  
  // Heartbeat LED
  digitalWrite(PIN_LED, (now / 250) % 2);
  
  // Run current test
  switch (currentTest) {
    case 0: testSplash(); break;
    case 1: testTextSizes(); break;
    case 2: testShapes(); break;
    case 3: testAnimation(); break;
    case 4: testPixelPattern(); break;
    case 5: testInfo(); break;
    case 6: testContrast(); break;
  }
  
  frameCount++;
  
  // Switch tests periodically
  if (now - lastSwitch > TEST_DURATION_MS) {
    currentTest = (currentTest + 1) % NUM_TESTS;
    lastSwitch = now;
    
    // Reset contrast when leaving that test
    if (currentTest == 0) {
      u8g2.setContrast(255);
    }
    
    Serial.printf("Test %d/%d: ", currentTest + 1, NUM_TESTS);
    switch (currentTest) {
      case 0: Serial.println("Splash"); break;
      case 1: Serial.println("Text Sizes"); break;
      case 2: Serial.println("Shapes"); break;
      case 3: Serial.println("Animation"); break;
      case 4: Serial.println("Pixel Pattern"); break;
      case 5: Serial.println("Info"); break;
      case 6: Serial.println("Contrast"); break;
    }
  }
  
  // Small delay - animation test needs fast updates
  delay(currentTest == 3 ? 16 : 50);
}