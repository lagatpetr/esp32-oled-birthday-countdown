/*
  ESP32 Birthday Bomb Timer
  -------------------------
  - Bomb-style beeping countdown on an OLED, speeding up toward the end
  - BOOM, then Happy Birthday plays on a passive buzzer
  - Lively OLED show: wavy title, name that jumps with every note,
    shine sweep, chasing border lights, twinkling stars, fireworks,
    floating balloons, music notes, a flickering cake and confetti
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
const char* NAME = "Afandi";        // <-- change this to the birthday person's name
const int OCTAVE_SHIFT = 1;           // 1 = play one octave higher (clearer on small buzzers), 0 = original pitch
const int SONG_REPEATS = 1;           // how many times the full song plays

#define SDA_PIN    21
#define SCL_PIN    22
#define BUZZER_PIN 18                 // buzzer + to D18, - to GND

// LEDs: long leg -> 220 ohm resistor -> GPIO, short leg -> GND
const int LED_PINS[] = {19, 23, 4, 15};
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
// beats are counted in sixteenth notes: 1 = sixteenth, 3 = dotted eighth,
// 4 = quarter, 8 = half, 12 = dotted half (3/4 time, like the real song)
struct Note { int freq; int beats; };

const Note melody[] = {
  // Happy birthday to you
  {NOTE_G4, 3}, {NOTE_G4, 1}, {NOTE_A4, 4}, {NOTE_G4, 4}, {NOTE_C5, 4}, {NOTE_B4, 12},
  // Happy birthday to you
  {NOTE_G4, 3}, {NOTE_G4, 1}, {NOTE_A4, 4}, {NOTE_G4, 4}, {NOTE_D5, 4}, {NOTE_C5, 12},
  // Happy birthday dear (name)
  {NOTE_G4, 3}, {NOTE_G4, 1}, {NOTE_G5, 4}, {NOTE_E5, 4}, {NOTE_C5, 4}, {NOTE_B4, 4}, {NOTE_A4, 8},
  // Happy birthday to you
  {NOTE_F5, 3}, {NOTE_F5, 1}, {NOTE_E5, 4}, {NOTE_C5, 4}, {NOTE_D5, 4}, {NOTE_C5, 12}
};
const int melodyLength = sizeof(melody) / sizeof(melody[0]);
const int SIXTEENTH_MS = 140;      // song speed: lower = faster, higher = slower
const int NOTE_GAP_MS  = 25;       // tiny silence between notes so repeated notes are clear

// =====================================================
//                    ANIMATION DATA
// =====================================================

// Balloons floating up on both sides
const int BALLOON_COUNT = 4;
float balloonX[BALLOON_COUNT] = {10, 24, 104, 118};
float balloonY[BALLOON_COUNT] = {60, 45, 55, 70};
float balloonSpeed[BALLOON_COUNT] = {0.8, 1.3, 1.0, 1.5};
bool  balloonFilled[BALLOON_COUNT] = {true, false, true, false};

// Confetti shooting from the cake
const int CONF_COUNT = 16;
struct Particle { float x, y, vx, vy; };
Particle conf[CONF_COUNT];

const int CAKE_CX = 64;       // cake centre x
const int CANDLE_TOP_Y = 38;  // where confetti shoots from

// Music notes that float out of the cake with every song note
const int FLOATER_COUNT = 5;
struct Floater { float x, y, vx, vy; bool alive; };
Floater floaters[FLOATER_COUNT];

// One firework at a time
struct Firework { bool active; int cx, cy, age; };
Firework fw;

// Twinkling stars
const int STAR_COUNT = 6;
const int starX[STAR_COUNT] = {33, 95, 20, 108, 46, 82};
const int starY[STAR_COUNT] = {22, 22, 38, 38, 34, 34};

// Time of the last beat (every song note triggers one)
unsigned long lastBeatMs = 0;

void spawnParticle(int i) {
  conf[i].x  = CAKE_CX + random(-3, 4);
  conf[i].y  = CANDLE_TOP_Y;
  conf[i].vx = random(-35, 36) / 10.0;
  conf[i].vy = -(random(25, 55) / 10.0);
}

void spawnMusicNote() {
  for (int i = 0; i < FLOATER_COUNT; i++) {
    if (!floaters[i].alive) {
      floaters[i].alive = true;
      floaters[i].x  = CAKE_CX + random(-4, 5);
      floaters[i].y  = 34;
      floaters[i].vx = (random(0, 2) ? 1 : -1) * (random(6, 14) / 10.0);
      floaters[i].vy = -(random(3, 6) / 10.0);
      return;
    }
  }
}

// A beat: the name jumps, a ring pulses from the cake, a note floats
// out and a few extra confetti pieces shoot up
void beat() {
  lastBeatMs = millis();
  spawnMusicNote();
  for (int k = 0; k < 3; k++) spawnParticle(random(0, CONF_COUNT));
}

void animationInit() {
  randomSeed(esp_random());
  for (int i = 0; i < CONF_COUNT; i++) {
    spawnParticle(i);
    conf[i].y -= random(0, 10);   // spread them out
  }
  for (int i = 0; i < FLOATER_COUNT; i++) floaters[i].alive = false;
  fw.active = false;
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
  for (int i = 0; i < FLOATER_COUNT; i++) {
    if (!floaters[i].alive) continue;
    floaters[i].x += floaters[i].vx;
    floaters[i].y += floaters[i].vy;
    if (floaters[i].y < 18 || floaters[i].x < 4 || floaters[i].x > 123) floaters[i].alive = false;
  }
  if (!fw.active) {
    if (random(0, 100) < 4) {                  // roughly one firework per second
      fw.active = true;
      fw.cx = random(14, 114);
      fw.cy = random(20, 44);
      fw.age = 0;
    }
  } else if (++fw.age > 14) {
    fw.active = false;
  }
}

// =====================================================
//                       DRAWING
// =====================================================

// Position along the screen edge (0 .. 379), used for the chasing lights
void edgePixel(int pos) {
  int x, y;
  if (pos < 127)      { x = pos;              y = 0; }
  else if (pos < 190) { x = 127;              y = pos - 127; }
  else if (pos < 317) { x = 127 - (pos - 190); y = 63; }
  else                { x = 0;                y = 63 - (pos - 317); }
  display.drawPixel(x, y);
}

void drawMarquee(unsigned long t) {
  int offset = (t / 25) % 8;                   // lights run around the border
  for (int k = 0; k < 47; k++) {
    int pos = (k * 8 + offset) % 380;
    edgePixel(pos);
    edgePixel((pos + 1) % 380);
  }
}

void drawStars(unsigned long t) {
  for (int i = 0; i < STAR_COUNT; i++) {
    int ph = (t / 130 + i * 3) % 8;
    int s = (ph < 4) ? ph : 8 - ph;            // 0..4..0 twinkle cycle
    if (s == 0) continue;
    int x = starX[i], y = starY[i];
    display.drawPixel(x, y);
    if (s >= 2) { display.drawHLine(x - 1, y, 3); display.drawVLine(x, y - 1, 3); }
    if (s >= 4) { display.drawHLine(x - 3, y, 7); display.drawVLine(x, y - 3, 7); }
  }
}

void drawFireworks() {
  if (!fw.active) return;
  float r = 2 + fw.age * 0.75;
  if (fw.age < 3) display.drawDisc(fw.cx, fw.cy, 1);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785398;                    // every 45 degrees
    display.drawPixel(fw.cx + (int)(cos(a) * r), fw.cy + (int)(sin(a) * r));
    if (fw.age > 4) {
      display.drawPixel(fw.cx + (int)(cos(a + 0.39) * r * 0.55),
                        fw.cy + (int)(sin(a + 0.39) * r * 0.55));
    }
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

void drawCake(unsigned long t) {
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
  // sprinkles that blink on the top layer
  if ((t / 200) % 2 == 0) { display.drawPixel(56, 49); display.drawPixel(70, 50); }
  else                    { display.drawPixel(60, 48); display.drawPixel(73, 49); }
  // candle
  display.drawBox(CAKE_CX - 1, 40, 3, 6);
  // flame: height and lean change all the time
  int h    = 3 + (t / 80) % 3;                 // 3..5
  int lean = (int)((t / 130) % 3) - 1;         // -1, 0, 1
  display.drawTriangle(CAKE_CX - 2, 39, CAKE_CX + 2, 39, CAKE_CX + lean, 38 - h);
  // little glow dots beside the flame
  if ((t / 110) % 2 == 0) { display.drawPixel(CAKE_CX - 5, 36); display.drawPixel(CAKE_CX + 5, 36); }
}

void drawConfetti() {
  for (int i = 0; i < CONF_COUNT; i++) {
    int x = (int)conf[i].x;
    int y = (int)conf[i].y;
    if (i % 2) display.drawBox(x, y, 2, 2);
    else       display.drawPixel(x, y);
  }
}

void drawFloaters(unsigned long t) {
  for (int i = 0; i < FLOATER_COUNT; i++) {
    if (!floaters[i].alive) continue;
    int x = (int)floaters[i].x + (int)(2.0 * sin(t / 120.0 + i));
    int y = (int)floaters[i].y;
    display.drawDisc(x, y, 1);                  // note head
    display.drawVLine(x + 1, y - 7, 7);         // stem
    display.drawLine(x + 1, y - 7, x + 4, y - 4); // flag
  }
}

// One full birthday frame
void drawBirthdayFrame() {
  unsigned long t = millis();
  display.clearBuffer();

  drawMarquee(t);
  drawStars(t);
  drawFireworks();
  drawBalloons(t);
  drawCake(t);

  // ring that pulses out of the cake on every beat
  unsigned long since = t - lastBeatMs;
  if (since < 260) display.drawCircle(CAKE_CX, 40, 5 + (int)(since / 22));

  drawConfetti();
  drawFloaters(t);

  // Title: every letter rides a wave
  display.setFont(u8g2_font_6x12_tr);
  const char* title = "HAPPY BIRTHDAY";
  int titleLen = 14;
  int titleX = (128 - titleLen * 6) / 2;
  for (int i = 0; i < titleLen; i++) {
    int y = 12 + (int)(2.0 * sin(t / 160.0 + i * 0.55));
    display.drawGlyph(titleX + i * 6, y, title[i]);
  }

  // Name: jumps up on every beat, with a shine sweeping across it
  display.setFont(u8g2_font_helvB10_tr);
  int nameW = display.getStrWidth(NAME);
  int nameX = (128 - nameW) / 2;
  int jump = 0;
  if (since < 220) jump = (int)(5.0 * (1.0 - since / 220.0));
  int nameY = 30 - jump;
  display.drawStr(nameX, nameY, NAME);

  unsigned long cyc = t % 2600;
  if (cyc < 900) {
    int sx = nameX - 8 + (int)(cyc * (nameW + 16) / 900);
    int x0 = (sx > nameX) ? sx : nameX;
    int x1 = (sx + 6 < nameX + nameW) ? sx + 6 : nameX + nameW;
    if (x1 > x0) {
      display.setDrawColor(2);                  // XOR = the letters flash inverted
      display.drawBox(x0, nameY - 10, x1 - x0, 12);
      display.setDrawColor(1);
    }
  }

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

// ---------- Play the song on an exact timeline ----------
unsigned long frameCostMs = 30;    // measured time of one OLED frame

// Draw one animation frame and remember how long it took
void drawTimedFrame() {
  unsigned long startMs = millis();
  drawBirthdayFrame();
  frameCostMs = (millis() - startMs) + 3;
}

// Play one note. Timing comes from a fixed schedule, so drawing the
// animation can never stretch a note or make the rhythm drift.
void playNoteAnimated(int freq, unsigned long noteStart, unsigned long durMs) {
  unsigned long soundEnd = noteStart + durMs - NOTE_GAP_MS;
  unsigned long noteEnd  = noteStart + durMs;

  buzzerTone(freq << OCTAVE_SHIFT);
  bool soundOn = (freq > 0);
  if (soundOn) beat();                          // name jumps, ring pulses, note floats

  while (millis() < noteEnd) {
    unsigned long now = millis();
    if (soundOn && now >= soundEnd) {
      buzzerOff();               // short gap between notes
      soundOn = false;
    }
    unsigned long nextEvent = soundOn ? soundEnd : noteEnd;
    // only draw a frame if it will finish before the next sound event
    if (nextEvent > now && (nextEvent - now) > frameCostMs) drawTimedFrame();
    else delay(1);
  }
  buzzerOff();
}

void playBirthday() {
  unsigned long noteStart = millis();

  for (int r = 0; r < SONG_REPEATS; r++) {
    for (int i = 0; i < melodyLength; i++) {
      ledOnly(i % LED_COUNT);    // a different LED lights for each note
      unsigned long dur = (unsigned long)melody[i].beats * SIXTEENTH_MS;
      playNoteAnimated(melody[i].freq, noteStart, dur);
      noteStart += dur;
    }
    if (r < SONG_REPEATS - 1) {  // short pause before the next repeat
      delay(800);
      noteStart = millis();
    }
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
  // After the song: the show keeps going, with a beat every half second
  static int pos = 0;
  static unsigned long lastLed = 0;
  static unsigned long lastBeat = 0;

  drawBirthdayFrame();

  if (millis() - lastBeat > 550) {
    lastBeat = millis();
    beat();
  }
  if (millis() - lastLed > 250) {
    lastLed = millis();
    ledOnly(pos);
    pos = (pos + 1) % LED_COUNT;
  }
}