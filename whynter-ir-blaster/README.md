# Whynter IR Blaster

ESP32-based IR blaster for controlling a Whynter portable AC unit.

## Wiring

- GPIO4 -> 100 ohm resistor -> IR LED anode (long leg)
- IR LED cathode (short leg) -> GND

## Usage

Press the BOOT button (GPIO0) to cycle through modes:
1. First press: spam AC ON signal
2. Second press: spam AC OFF signal
3. Third press: stop

Signals are sent every 250ms while active.

## IR Codes

Raw timing data captured from a Flipper Zero. The `Whynter.ir` file contains the original Flipper format captures.

## Dependencies

- IRremoteESP8266 (install via Arduino Library Manager)
