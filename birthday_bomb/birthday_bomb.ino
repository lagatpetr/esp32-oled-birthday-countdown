/*
  ESP32 Birthday Bomb Timer
  -------------------------
  - Bomb-style beeping countdown on an OLED, speeding up toward the end
  - BOOM, then Happy Birthday plays on a passive buzzer
  - OLED shows balloons, a cake with a flickering candle and confetti
  - LEDs flash with the beeps and chase with the melody

  Hardware: ESP32 DevKit (30-pin), 128x64 I2C OLED (SSD1306),
            passive buzzer, 4 LEDs + 4 x 220 ohm resistors
  Library:  U8g2 (install from the Arduino Library Manager)

  Wiring (all on the D21/D22 side of the board):
    OLED SDA -> D21      OLED SCL -> D22
    OLED VCC -> 3V3      OLED GND -> GND
    Buzzer + -> D18      Buzzer - -> GND
    LED 1 -> D19, LED 2 -> D23, LED 3 -> D4, LED 4 -> D16
    (each LED: long leg -> 220 ohm resistor -> pin, short leg -> GND)
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

// ================= SETTINGS =================
const int COUNTDOWN_SECONDS = 8;      // countdown starts from this number
const char* NAME = "YourName";        // <-- change this to the birthday person's name

#define SDA_PIN    21
#define SCL_PIN    22
#define BUZZER_PIN 18                 // buzzer + to D18, - to GND

// LEDs: long leg -> 220 ohm resistor -> GPIO, short leg -> GND
const int LED_PINS[] = {19, 23, 4, 16};
const int LED_COUNT = sizeof(LED_PINS) / sizeof(LED_PINS[0]);
// ============================================

// 128x64 SSD1306 I2C OLED
// (for a 1.3" SH1106 screen use: U8G2_SH1106_128X64_NONAME_F_HW_I2C)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ---------- Note frequencies ----------
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define REST     0

// ---------- LED helpers ----------
void ledsInit() {
  for (int i = 0; i < LED_COUNT; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
}

void ledsAll(bool on) {
  for (int i = 0; i < LED_COUNT; i++) digitalWrite(LED_PINS[i], on ? HIGH : LOW);
}

void ledOnly(int index) {
  for (int i = 0; i < LED_COUNT; i++) digitalWrite(LED_PINS[i], (i == index) ? HIGH : LOW);
}

// ---------- Buzzer helpers (works on ESP32 core 2.x and 3.x) ----------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  void buzzerInit()            { ledcAttach(BUZZER_PIN, 2000, 10); }
  void buzzerTone(int freq)    { ledcWriteTone(BUZZER_PIN, freq); }
#else
  #define BUZZER_CHANNEL 0
  void buzzerInit()            { ledcSetup(BUZZER_CHANNEL, 2000, 10); ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL); }
  void buzzerTone(int freq)    { ledcWriteTone(BUZZER_CHANNEL, freq); }
#endif

void buzzerOff() { buzzerTone(0); }

// Beep with all LEDs flashing together
void beep(int freq, int durationMs) {
  ledsAll(true);
  buzzerTone(freq);
  delay(durationMs);
  buzzerOff();
  ledsAll(false);
}

// ---------- Happy Birthday melody ----------
struct Note { int freq; int beats; };   // beats in quarter-beat units (4 = one full beat)

const Note melody[] = {
  // Happy birthday to you
  {NOTE_G4, 3}, {NOTE_G4, 1}, {NOTE_A4, 4}, {NOTE_G4, 4}, {NOTE_C5, 4}, {NOTE_B4, 8},
  // Happy birthday to you
  {NOTE_G4, 3}, {NOTE_G4, 1}, {NOTE_A4, 4}, {NOTE_G4, 4}, {NOTE_D5, 4}, {NOTE_C5, 8},
  // Happy birthday dear (name)
  {NOTE_G4, 3}, {NOTE_G4, 1}, {NOTE_G5, 4}, {NOTE_E5, 4}, {NOTE_C5, 4}, {NOTE_B4, 4}, {NOTE_A4, 8},
  // Happy birthday to you
  {NOTE_F5, 3}, {NOTE_F5, 1}, {NOTE_E5, 4}, {NOTE_C5, 4}, {NOTE_D5, 4}, {NOTE_C5, 8}
};
const int melodyLength = sizeof(melody) / sizeof(melody[0]);
const int QUARTER_BEAT_MS = 130;   // lower = faster song, higher = slower

// ---------- Animation data ----------
// Balloons
const int BALLOON_COUNT = 4;
float balloonX[BALLOON_COUNT] = {10, 24, 104, 118};
float balloonY[BALLOON_COUNT] = {60, 45, 55, 70};
float balloonSpeed[BALLOON_COUNT] = {0.8, 1.3, 1.0, 1.5};
bool  balloonFilled[BALLOON_COUNT] = {true, false, true, false};

// Confetti shooting from the cake
const int CONF_COUNT = 14;
struct Particle { float x, y, vx, vy; };
Particle conf[CONF_COUNT];

const int CAKE_CX = 64;       // cake centre x
const int CANDLE_TOP_Y = 38;  // where confetti shoots from

void spawnParticle(int i) {
  conf[i].x  = CAKE_CX + random(-3, 4);
  conf[i].y  = CANDLE_TOP_Y;
  conf[i].vx = random(-35, 36) / 10.0;
  conf[i].vy = -(random(25, 55) / 10.0);
}

void animationInit() {
  randomSeed(esp_random());
  for (int i = 0; i < CONF_COUNT; i++) {
    spawnParticle(i);
    // spread them out so they don't all fire at once
    conf[i].y -= random(0, 10);
  }
}

// ---------- Drawing ----------
void drawCake(bool flameBig) {
  // plate
  display.drawBox(38, 62, 52, 2);
  // bottom layer
  display.drawBox(44, 53, 40, 9);
  // top layer
  display.drawBox(52, 46, 24, 7);
  // icing drips (gaps) on bottom layer
  display.setDrawColor(0);
  display.drawPixel(50, 54); display.drawPixel(58, 54);
  display.drawPixel(66, 54); display.drawPixel(74, 54);
  display.drawPixel(80, 54);
  display.setDrawColor(1);
  // candle
  display.drawBox(CAKE_CX - 1, 40, 3, 6);
  // flame (flickers)
  if (flameBig) {
    display.drawTriangle(CAKE_CX - 2, 39, CAKE_CX + 2, 39, CAKE_CX, 33);
  } else {
    display.drawTriangle(CAKE_CX - 1, 39, CAKE_CX + 1, 39, CAKE_CX, 35);
  }
}

void drawBalloons(unsigned long t) {
  for (int i = 0; i < BALLOON_COUNT; i++) {
    int bx = (int)(balloonX[i] + 3.0 * sin((t / 250.0) + i * 1.7));
    int by = (int)balloonY[i];
    if (balloonFilled[i]) display.drawDisc(bx, by, 5);
    else                  display.drawCircle(bx, by, 5);
    // string
    int wob = (int)(3.0 * sin((t / 200.0) + i));
    display.drawLine(bx, by + 5, bx + wob, by + 15);
  }
}

void drawConfetti() {
  for (int i = 0; i < CONF_COUNT; i++) {
    int x = (int)conf[i].x;
    int y = (int)conf[i].y;
    if (i % 2) display.drawBox(x, y, 2, 2);
    else       display.drawPixel(x, y);
  }
}

void updateAnimation() {
  for (int i = 0; i < BALLOON_COUNT; i++) {
    balloonY[i] -= balloonSpeed[i];
    if (balloonY[i] < 22) balloonY[i] = 72;   // wrap around from the bottom
  }
  for (int i = 0; i < CONF_COUNT; i++) {
    conf[i].x  += conf[i].vx;
    conf[i].y  += conf[i].vy;
    conf[i].vy += 0.25;                        // gravity
    if (conf[i].y > 63 || conf[i].x < 0 || conf[i].x > 127) spawnParticle(i);
  }
}

// One full birthday frame: small title, smaller name, balloons, cake, confetti
void drawBirthdayFrame() {
  unsigned long t = millis();
  display.clearBuffer();

  // Title
  display.setFont(u8g2_font_6x12_tr);
  int w1 = display.getStrWidth("HAPPY BIRTHDAY");
  display.drawStr((128 - w1) / 2, 11, "HAPPY BIRTHDAY");

  // Name
  display.setFont(u8g2_font_helvB10_tr);
  int w2 = display.getStrWidth(NAME);
  display.drawStr((128 - w2) / 2, 27, NAME);

  drawBalloons(t);
  drawCake((t / 120) % 2 == 0);
  drawConfetti();

  display.sendBuffer();
  updateAnimation();
}

// ---------- Countdown screens ----------
void showCountdown(int n) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(30, 12, "BOMB TIMER");

  display.setFont(u8g2_font_logisoso42_tn);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", n);
  int w = display.getStrWidth(buf);
  display.drawStr((128 - w) / 2, 62, buf);

  display.drawFrame(0, 0, 128, 64);
  display.sendBuffer();
}

void showBoom() {
  display.clearBuffer();
  display.setFont(u8g2_font_logisoso28_tr);
  int w = display.getStrWidth("BOOM!");
  display.drawStr((128 - w) / 2, 46, "BOOM!");
  display.drawFrame(0, 0, 128, 64);
  display.drawFrame(3, 3, 122, 58);
  display.sendBuffer();
}

// ---------- Countdown with bomb beeps ----------
void runCountdown() {
  for (int s = COUNTDOWN_SECONDS; s >= 1; s--) {
    showCountdown(s);

    if (s > 3) {
      // slow beep: one per second
      beep(1500, 120);
      delay(880);
    } else if (s == 3) {
      // faster: two beeps per second
      beep(1800, 100); delay(350);
      beep(1800, 100); delay(350);
    } else if (s == 2) {
      // even faster: four beeps per second
      for (int i = 0; i < 4; i++) { beep(2100, 70); delay(170); }
    } else {
      // last second: rapid beeping
      for (int i = 0; i < 6; i++) { beep(2400, 50); delay(110); }
    }
  }

  // BOOM: long low tone with LEDs on, then a quick flash burst
  showBoom();
  ledsAll(true);
  buzzerTone(300);
  delay(700);
  buzzerOff();
  for (int i = 0; i < 6; i++) {
    ledsAll(false); delay(80);
    ledsAll(true);  delay(80);
  }
  ledsAll(false);
  delay(300);
}

// ---------- Play one note while the animation keeps running ----------
void playNoteAnimated(int freq, int durMs) {
  unsigned long start = millis();
  unsigned long soundEnd = durMs * 0.9;

  buzzerTone(freq);
  bool soundOn = true;

  while (millis() - start < (unsigned long)durMs) {
    if (soundOn && millis() - start >= soundEnd) {
      buzzerOff();               // short gap between notes
      soundOn = false;
    }
    drawBirthdayFrame();
  }
  if (soundOn) buzzerOff();
}

// ---------- Play the song ----------
void playBirthday() {
  for (int i = 0; i < melodyLength; i++) {
    ledOnly(i % LED_COUNT);      // a different LED lights for each note
    playNoteAnimated(melody[i].freq, melody[i].beats * QUARTER_BEAT_MS);
  }
  ledsAll(false);
}

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);         // faster I2C = smoother animation
  display.begin();
  buzzerInit();
  buzzerOff();
  ledsInit();
  animationInit();

  // Ready screen
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(20, 36, "Get ready...");
  display.sendBuffer();
  delay(1500);

  runCountdown();
  playBirthday();
}

void loop() {
  // After the song: animation keeps going, LEDs chase
  static int pos = 0;
  static unsigned long lastLed = 0;

  drawBirthdayFrame();

  if (millis() - lastLed > 250) {
    lastLed = millis();
    ledOnly(pos);
    pos = (pos + 1) % LED_COUNT;
  }
}
