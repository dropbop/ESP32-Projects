# Inland OLED Testing

Test suite for the Inland 1.3" 128x64 OLED display (SH1106 driver).

## Hardware

- ESP32 Dev Module
- [Inland IIC/SPI 1.3" 128x64 OLED V2.0](https://www.microcenter.com/product/643965/inland-iic-spi-13-128x64-oled-v20-graphic-display-module-for-arduino-uno-r3) (SKU: 345785, Mfr Part#: KS0056)

**Note:** This display uses the SH1106 driver, not SSD1306. Requires 5V on VCC (3.3V may not work).

## Wiring (Software SPI)

| OLED | ESP32 |
|------|-------|
| GND | GND |
| VCC | VIN (5V) |
| CLK | GPIO 25 |
| MOSI | GPIO 26 |
| RES | GPIO 12 |
| DC | GPIO 14 |
| CS | GPIO 27 |

## Setup

1. Install library: **U8g2** (Library Manager)
2. Upload to ESP32

## Tests

Cycles through 7 display tests every 2.5 seconds:

1. Splash screen
2. Text sizes (various fonts)
3. Shapes (rectangles, circles, triangles, lines)
4. Animation (bouncing ball, spinning line, progress bar)
5. Pixel patterns (checkerboard, dithering)
6. Display info
7. Contrast cycling
