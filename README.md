# ESP32 Birthday Bomb Timer

A fun birthday gift project: an ESP32 runs a bomb-style countdown with speeding-up beeps, goes BOOM, then plays Happy Birthday while the OLED shows balloons, a cake with a flickering candle and confetti. LEDs flash with the beeps and chase with the melody.

## Parts
- ESP32 DevKit (30-pin)
- 128x64 I2C OLED (SSD1306; SH1106 for 1.3" screens)
- Passive buzzer (an active buzzer cannot play the melody)
- 4 LEDs + 4 x 220 ohm resistors
- Breadboard and jumper wires

## Wiring
All parts are on the same side of the board as D21/D22.

| Part | ESP32 pin |
|---|---|
| OLED SDA | D21 |
| OLED SCL | D22 |
| OLED VCC | 3V3 |
| OLED GND | GND |
| Buzzer + | D18 |
| Buzzer - | GND |
| LED 1 / 2 / 3 / 4 | D19 / D23 / D4 / D16 (through 220 ohm) |

All grounds (OLED, buzzer, LED short legs) go to GND. Use a breadboard ground rail since the board has only one GND pin on that side.

## Setup
1. Install the **U8g2** library in the Arduino IDE (Library Manager).
2. Open `birthday_bomb/birthday_bomb.ino` and change `NAME` to the birthday person's name.
3. Optionally change `COUNTDOWN_SECONDS`.
4. Select your ESP32 board and upload.

The countdown starts as soon as the ESP32 is powered, so plug it in at the moment you want it to begin.

## Customising
- `COUNTDOWN_SECONDS`: length of the bomb countdown
- `QUARTER_BEAT_MS`: song speed (lower is faster)
- `CONF_COUNT`: amount of confetti
- `balloonSpeed`: how fast the balloons rise
- `LED_PINS`: add or remove LED pins
- 1.3" SH1106 screen: use `U8G2_SH1106_128X64_NONAME_F_HW_I2C` instead of the SSD1306 line

## Troubleshooting
- **Blank screen:** check SDA/SCL are not swapped and the OLED is on 3V3. Try the SH1106 constructor for 1.3" screens.
- **Only one beep tone, no melody:** your buzzer is active. Use a passive buzzer.
- **Boot problems:** avoid TX0, RX0, D2, D5 and D15 for LEDs or the buzzer.

## License
MIT
