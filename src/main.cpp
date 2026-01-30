#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h"

enum IdleMode { IDLE_EYES, IDLE_QUOTE };
IdleMode idleMode = IDLE_EYES;
unsigned long lastIdleSwitch = 0;
#define IDLE_SWITCH_TIME 5000

bool blinkNow = false;
unsigned long blinkUntil = 0;

int currentEyeR = 14;
int currentPupilR = 5;
int currentSpacing = 65;

enum EyeEmotion {
  E_NEUTRAL, E_HAPPY, E_ANGRY, E_SLEEPY, E_SURPRISE,
  E_LOOK_LEFT, E_LOOK_RIGHT, E_LOOK_UP, E_LOOK_DOWN
};
EyeEmotion currentEmotion = E_NEUTRAL;
unsigned long lastEmotionChange = 0;

// --- Таймеры и состояния ---
unsigned long lastActivity = 0;
bool isIdle = false;
unsigned long lastKeyTime = 0;
bool alarmSkipToday = false; 

// Для цифрового ввода
int digitBuffer = -1; 
unsigned long lastDigitTime = 0;
#define DIGIT_TIMEOUT 2000

#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"
#include "fonts/FontsRus/FreeSans9.h"

#define FONT_TIME   &FreeSansBold14pt8b
#define FONT_HEADER &FreeSansBold9pt8b
#define FONT_TEXT   &FreeSans9pt8b

#define TFT_WIDTH  160
#define TFT_HEIGHT 80
#define IDLE_TIMEOUT 15000

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);
GFXcanvas16 canvas(TFT_WIDTH, TFT_HEIGHT);

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

struct Theme { uint16_t top, bot, text, accent, panel, border; };
Theme thNight  = { RGB(10,15,30),  RGB(5,8,15),   RGB(220,230,255), RGB(255,220,100), RGB(30,40,60),  RGB(80,100,150) };
Theme thWinter = { RGB(40,60,100), RGB(20,30,60), RGB(230,240,255), RGB(150,220,255), RGB(40,50,80),  RGB(120,150,200) };
Theme thSpring = { RGB(30,60,30),  RGB(15,35,15), RGB(220,255,220), RGB(120,240,120), RGB(35,55,35),  RGB(100,180,100) };
Theme thSummer = { RGB(20,50,100), RGB(10,30,70), RGB(240,255,255), RGB(255,200,80),  RGB(25,45,85),  RGB(80,140,200) };
Theme thAutumn = { RGB(60,35,15),  RGB(30,20,10), RGB(255,240,220), RGB(255,160,60),  RGB(50,35,20),  RGB(150,90,40) };
Theme* currentTheme = &thNight;

enum AppMode { MODE_CLOCK, MODE_MENU, MODE_SET_TIME, MODE_SET_ALARM, MODE_QUOTES, MODE_RING };
AppMode currentMode = MODE_CLOCK;

struct SoftClock { int hour, minute, second, month; unsigned long lastMillis; };
struct SingleAlarm { uint8_t hour, minute; bool active; };

SoftClock rtc = {12, 0, 0, 1, 0};
SingleAlarm singleAlarm = {7, 0, false};
SoftClock tempRtc = {12, 0, 0, 1, 0};
SingleAlarm tempAlarm = {7, 0, false};
int menuCursor = 0;
int settingField = 0; 
float temp = 22.0, hum = 45.0;
bool lampState = false;
unsigned long lastDraw = 0, lastSensor = 0, popupTimer = 0;
String popupMsg = "";
long nextBlink = 0;

const char* quotes[] = {
  "Улыбнись!", "Удачи!", "Верь!", "Привет!",
  "Сможешь!", "Действуй!", "Сияй!", "Успеха!",
  "Отдохни", "Мечтай!", "Вперёд!", "Твой день!"
};
int quoteIdx = 0;
unsigned long soundTimer = 0;
bool isRinging = false;
bool buzzerState = false;

String utf8rus(String source) {
  String target = "";
  for (int i = 0; i < (int)source.length(); i++) {
    unsigned char n = source[i];
    if (n < 127) target += (char)n; 
    else if (n == 0xD0 && i+1 < (int)source.length()) {
      unsigned char n2 = source[++i];
      if (n2 == 0x81) target += (char)0xC0;
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2);
    }
    else if (n == 0xD1 && i+1 < (int)source.length()) {
      unsigned char n2 = source[++i];
      if (n2 == 0x91) target += (char)0xC1;
      else if (n2 >= 0x80 && n2 <= 0x8F) target += (char)(n2);
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2 - 0x10);
    }
  }
  return target;
}

int getDigit(unsigned long key) {
  if (key == IR_BTN_0) return 0; if (key == IR_BTN_1) return 1;
  if (key == IR_BTN_2) return 2; if (key == IR_BTN_3) return 3;
  if (key == IR_BTN_4) return 4; if (key == IR_BTN_5) return 5;
  if (key == IR_BTN_6) return 6; if (key == IR_BTN_7) return 7;
  if (key == IR_BTN_8) return 8; if (key == IR_BTN_9) return 9;
  return -1;
}

void showPopup(String msg) { popupMsg = msg; popupTimer = millis(); }

void toggleLamp() {
  lampState = !lampState;
  digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
  showPopup(lampState ? "Свет ВКЛ" : "Свет ВЫКЛ");
}

void updateThemeByEnv() {
  int h = rtc.hour;

  if (h >= 22 || h < 6) {
    currentTheme = &thNight;
    return;
  }

  if (temp > 28) {
    currentTheme = &thAutumn; // жарко (рыжая)
    return;
  }

  if (temp < 10) {
    currentTheme = &thWinter;
    return;
  }

  if (hum > 70) {
    currentTheme = &thSpring; // влажно / дождь
    return;
  }

  if (hum < 30) {
    currentTheme = &thSummer; // сухо / песок
    return;
  }

  currentTheme = &thSpring; // день
}

void updateTime() {
  unsigned long now = millis();
  if (now - rtc.lastMillis >= 1000) {
    rtc.second++; rtc.lastMillis = now;
    if (rtc.second >= 60) { rtc.second = 0; rtc.minute++; alarmSkipToday = false; }
    if (rtc.minute >= 60) { rtc.minute = 0; rtc.hour++; }
    if (rtc.hour >= 24) { rtc.hour = 0; rtc.month++; if(rtc.month > 12) rtc.month = 1; }
  }
}

void checkAlarm() {
  if (rtc.second != 0 || !singleAlarm.active || isRinging || alarmSkipToday) return;
  if (singleAlarm.hour == rtc.hour && singleAlarm.minute == rtc.minute) {
    currentMode = MODE_RING; isRinging = true; lampState = true;
    digitalWrite(PIN_RELAY, HIGH); singleAlarm.active = false;
  }
}

void playBirdSong() {
  if (!isRinging) return;
  if (millis() - soundTimer > 60) { 
    soundTimer = millis();
    buzzerState = !buzzerState;
    digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
  }
}

void drawRain() {
  for (int i = 0; i < 12; i++) {
    int x = random(TFT_WIDTH);
    int y = random(TFT_HEIGHT);
    canvas.drawFastVLine(x, y, random(4, 10), RGB(180, 200, 255));
  }
}

void drawSand() {
  for (int i = 0; i < 25; i++) {
    int x = random(TFT_WIDTH);
    int y = random(TFT_HEIGHT);
    canvas.drawPixel(x, y, RGB(220, 200, 140));
  }
}


void drawCenteredText(int y, String text, const GFXfont* font) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setFont(font);
  canvas.getTextBounds(utf8rus(text), 0, y, &x1, &y1, &w, &h);
  canvas.setCursor((TFT_WIDTH - w) / 2, y);
  canvas.print(utf8rus(text));
}

void drawIOSElement(int x, int y, int w, int h, String text, bool selected) {
  canvas.fillRoundRect(x, y, w, h, 6, currentTheme->panel);
  if (selected) {
    canvas.drawRoundRect(x, y, w, h, 6, ST7735_WHITE);
    canvas.drawRoundRect(x+1, y+1, w-2, h-2, 5, ST7735_WHITE);
    canvas.setTextColor(currentTheme->accent);
  } else {
    canvas.drawRoundRect(x, y, w, h, 6, currentTheme->border);
    canvas.setTextColor(currentTheme->text);
  }
  int16_t bx, by; uint16_t bw, bh;
  canvas.setFont(FONT_TEXT);
  canvas.getTextBounds(utf8rus(text), 0, 0, &bx, &by, &bw, &bh);
  canvas.setCursor(x + (w - bw) / 2, y + (h/2) + (bh/2) - 2);
  canvas.print(utf8rus(text));
}

void drawEyes() {
  int baseY = 42;
  // Убираем random(), используем глобальные переменные
  int spacing = currentSpacing; 
  int x1 = (TFT_WIDTH - spacing) / 2;
  int x2 = x1 + spacing;

  int eyeR = currentEyeR;
  int pupilR = currentPupilR;

  int dx = 0, dy = 0;
  bool closed = blinkNow;

  switch (currentEmotion) {
    case E_HAPPY: dy = -2; break;
    case E_ANGRY: dy = 2; break;
    case E_SLEEPY: closed = true; break;
    case E_LOOK_LEFT: dx = -4; break;
    case E_LOOK_RIGHT: dx = 4; break;
    case E_LOOK_UP: dy = -4; break;
    case E_LOOK_DOWN: dy = 4; break;
    default: break;
  }

  canvas.fillCircle(x1, baseY, eyeR, ST7735_WHITE);
  canvas.fillCircle(x2, baseY, eyeR, ST7735_WHITE);

  if (!closed) {
    canvas.fillCircle(x1 + dx, baseY + dy, pupilR, ST7735_BLACK);
    canvas.fillCircle(x2 + dx, baseY + dy, pupilR, ST7735_BLACK);
  } else {
    canvas.drawFastHLine(x1 - eyeR/2, baseY, eyeR, ST7735_BLACK);
    canvas.drawFastHLine(x2 - eyeR/2, baseY, eyeR, ST7735_BLACK);
  }

  if (currentEmotion == E_ANGRY) {
    canvas.drawLine(x1 - eyeR, baseY - eyeR, x1 + eyeR, baseY - eyeR/2, ST7735_BLACK);
    canvas.drawLine(x2 - eyeR, baseY - eyeR/2, x2 + eyeR, baseY - eyeR, ST7735_BLACK);
  }

  if (currentEmotion == E_HAPPY) {
    canvas.drawCircle(x1, baseY + eyeR/2, eyeR/2, ST7735_BLACK);
    canvas.drawCircle(x2, baseY + eyeR/2, eyeR/2, ST7735_BLACK);
  }
}

void drawClock() {
  uint16_t c1 = currentTheme->top, c2 = currentTheme->bot;
  for (int y = 0; y < TFT_HEIGHT; y++) {
    uint8_t r = ((c1>>11)&0x1F) + (((c2>>11)&0x1F) - ((c1>>11)&0x1F)) * y / TFT_HEIGHT;
    uint8_t g = ((c1>>5)&0x3F) + (((c2>>5)&0x3F) - ((c1>>5)&0x3F)) * y / TFT_HEIGHT;
    uint8_t b = (c1&0x1F) + ((c2&0x1F) - (c1&0x1F)) * y / TFT_HEIGHT;
    canvas.drawFastHLine(0, y, TFT_WIDTH, (r << 11) | (g << 5) | b);
  }
  if (hum > 70) drawRain();
  if (hum < 30) drawSand();


  if (isIdle) {
      drawEyes();
  } else {
    canvas.setTextColor(currentTheme->accent);
    char buf[6]; snprintf(buf, 6, "%02d:%02d", rtc.hour, rtc.minute);
    drawCenteredText(50, buf, FONT_TIME);
    canvas.setTextColor(currentTheme->text);
    drawCenteredText(75, String((int)temp) + "C " + String((int)hum) + "%", FONT_TEXT);
    if (singleAlarm.active) canvas.fillCircle(10, 10, 3, RGB(255, 50, 50));
    if (lampState) canvas.fillCircle(150, 10, 4, RGB(255, 220, 100));
  }
}

void drawMenu() {
  canvas.fillScreen(ST7735_BLACK);
  canvas.setTextColor(currentTheme->accent);
  drawCenteredText(18, "МЕНЮ", FONT_HEADER);
  const char* items[] = {"Время", "Буд.", "Свет", "Цитата"};
  for(int i = 0; i < 4; i++) {
    int x = 8 + (i % 2) * 76;
    int y = 28 + (i / 2) * 26;
    drawIOSElement(x, y, 70, 22, items[i], i == menuCursor);
  }
}

void drawSetTime() {
  canvas.fillScreen(ST7735_BLACK);
  drawCenteredText(18, "ВРЕМЯ", FONT_HEADER);
  String hStr = (tempRtc.hour < 10 ? "0" : "") + String(tempRtc.hour);
  String mStr = (tempRtc.minute < 10 ? "0" : "") + String(tempRtc.minute);
  drawIOSElement(35, 35, 40, 35, hStr, settingField == 0);
  drawIOSElement(85, 35, 40, 35, mStr, settingField == 1);
}

void drawSetAlarm() {
  canvas.fillScreen(ST7735_BLACK);
  drawCenteredText(18, "БУДИЛЬНИК", FONT_HEADER);
  String hStr = (tempAlarm.hour < 10 ? "0" : "") + String(tempAlarm.hour);
  String mStr = (tempAlarm.minute < 10 ? "0" : "") + String(tempAlarm.minute);
  drawIOSElement(10, 35, 35, 30, hStr, settingField == 0);
  drawIOSElement(50, 35, 35, 30, mStr, settingField == 1);
  drawIOSElement(95, 35, 55, 30, tempAlarm.active ? "ВКЛ" : "ВЫКЛ", settingField == 2);
}

void drawQuotesScreen() {
  canvas.fillScreen(ST7735_BLACK);
  int pW = 130, pH = 35;
  int pX = (TFT_WIDTH - pW) / 2, pY = (TFT_HEIGHT - pH) / 2;
  canvas.fillRoundRect(pX, pY, pW, pH, 8, currentTheme->panel);
  canvas.drawRoundRect(pX, pY, pW, pH, 8, ST7735_WHITE);
  canvas.setTextColor(ST7735_WHITE);
  drawCenteredText(pY + 22, quotes[quoteIdx], FONT_TEXT);
}

// Универсальная функция обработки цифрового ввода
void handleNumericInput(int& value, int maxVal, int digit) {
  unsigned long now = millis();
  if (digitBuffer != -1 && (now - lastDigitTime < DIGIT_TIMEOUT)) {
    int newVal = digitBuffer * 10 + digit;
    if (newVal < maxVal) {
      value = newVal;
      digitBuffer = -1; // Ввод завершен
    } else {
      // Если число слишком большое, считаем нажатую цифру за первую новую
      value = digit;
      digitBuffer = digit;
    }
  } else {
    value = digit;
    digitBuffer = digit;
  }
  lastDigitTime = now;
}

void handleIR() {
  if (!irrecv.decode(&results)) return;
  unsigned long key = results.value; unsigned long now = millis();
  if (now - lastKeyTime < 150) { irrecv.resume(); return; }
  lastKeyTime = now;
  
  if (currentMode == MODE_RING) {
    isRinging = false; digitalWrite(PIN_BUZZER, LOW); 
    digitalWrite(PIN_RELAY, LOW); lampState = false;
    currentMode = MODE_CLOCK; irrecv.resume(); return;
  }
  
  
  if (isIdle) {
    isIdle = false;
    lastActivity = now;
    irrecv.resume();
    return;
  }


  lastActivity = now;

  if (key == IR_BTN_RETURN || key == IR_BTN_CLOCK) { currentMode = MODE_CLOCK; irrecv.resume(); return; }
  if (key == IR_BTN_RESET) { singleAlarm.active = false; showPopup("Сброс"); irrecv.resume(); return; }
  if (key == IR_BTN_SUN) { toggleLamp(); irrecv.resume(); return; }

  int digit = getDigit(key);

  switch (currentMode) {
    case MODE_CLOCK:
      if (key == IR_BTN_EQ) { currentMode = MODE_MENU; menuCursor = 0; }
      if (key == IR_BTN_MESSAGE) { currentMode = MODE_QUOTES; quoteIdx = random(12); }
      break;
    case MODE_MENU:
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) menuCursor = (menuCursor + 3) % 4;
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) menuCursor = (menuCursor + 1) % 4;
      if (key == IR_BTN_OK) {
        digitBuffer = -1; // Сброс буфера перед входом
        if(menuCursor==0) { currentMode = MODE_SET_TIME; tempRtc = rtc; settingField = 0; }
        if(menuCursor==1) { currentMode = MODE_SET_ALARM; tempAlarm = singleAlarm; settingField = 0; }
        if(menuCursor==2) toggleLamp();
        if(menuCursor==3) { currentMode = MODE_QUOTES; quoteIdx = random(12); }
      }
      break;
    case MODE_SET_TIME:
      if (key == IR_BTN_PREV) { settingField = 0; digitBuffer = -1; }
      if (key == IR_BTN_NEXT) { settingField = 1; digitBuffer = -1; }
      if (digit != -1) {
        if (settingField == 0) handleNumericInput(tempRtc.hour, 24, digit);
        else handleNumericInput(tempRtc.minute, 60, digit);
      }
      if (key == IR_BTN_PLUS || key == IR_BTN_MINUS) {
        digitBuffer = -1;
        int diff = (key == IR_BTN_PLUS) ? 1 : -1;
        if(settingField==0) tempRtc.hour = (tempRtc.hour + diff + 24) % 24;
        else tempRtc.minute = (tempRtc.minute + diff + 60) % 60;
      }
      if (key == IR_BTN_OK) { rtc = tempRtc; currentMode = MODE_CLOCK; showPopup("ОК"); }
      break;
    case MODE_SET_ALARM:
      if (key == IR_BTN_PREV) { settingField = (settingField + 2) % 3; digitBuffer = -1; }
      if (key == IR_BTN_NEXT) { settingField = (settingField + 1) % 3; digitBuffer = -1; }
      if (digit != -1 && settingField != 2) {
        if (settingField == 0) handleNumericInput((int&)tempAlarm.hour, 24, digit);
        else handleNumericInput((int&)tempAlarm.minute, 60, digit);
      }
      if (key == IR_BTN_PLUS || key == IR_BTN_MINUS) {
        digitBuffer = -1;
        int diff = (key == IR_BTN_PLUS) ? 1 : -1;
        if(settingField==0) tempAlarm.hour = (tempAlarm.hour + diff + 24) % 24;
        else if(settingField==1) tempAlarm.minute = (tempAlarm.minute + diff + 60) % 60;
        else tempAlarm.active = !tempAlarm.active;
      }
      if (key == IR_BTN_OK) { 
        singleAlarm = tempAlarm; 
        if (singleAlarm.hour == rtc.hour && singleAlarm.minute == rtc.minute) alarmSkipToday = true;
        currentMode = MODE_CLOCK; showPopup("Буд. ОК"); 
      }
      break;
    case MODE_QUOTES:
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) quoteIdx = (quoteIdx + 1) % 12;
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) quoteIdx = (quoteIdx + 11) % 12;
      break;
  }
  irrecv.resume();
}

void setup() {
  pinMode(PIN_RELAY, OUTPUT); digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  dht.begin(); irrecv.enableIRIn();
  tft.initR(INITR_MINI160x80); tft.setRotation(3);
  rtc.lastMillis = millis(); lastActivity = millis();
}

void loop() {
  updateTime();
  checkAlarm();
  handleIR();

  if (currentMode == MODE_CLOCK && !isRinging && millis() - lastActivity > IDLE_TIMEOUT)
      isIdle = true;

  // ----------------------------
  // Смена эмоций и параметров глаз
  if (millis() - lastEmotionChange > 4000) {
      // Пул эмоций с повышенным шансом на "милые"
      EyeEmotion emotionsPool[] = {
        E_NEUTRAL, E_NEUTRAL, // Нейтральные
        E_HAPPY, E_HAPPY, E_HAPPY, // Счастливые (х3 шанс)
        E_SURPRISE, // Удивление
        E_LOOK_LEFT, E_LOOK_RIGHT, E_LOOK_UP, E_LOOK_DOWN, // Взгляд по сторонам
        E_SLEEPY // Сонные
        // E_ANGRY мы просто убрали из пула
      };
      currentEmotion = emotionsPool[random(sizeof(emotionsPool)/sizeof(EyeEmotion))];
      
      // Обновляем параметры глаз только при смене эмоции
      currentEyeR = random(13, 16);
      currentPupilR = random(4, 6);
      currentSpacing = random(60, 70);

      lastEmotionChange = millis();
  }

  // Моргание глаз
  if (millis() > blinkUntil && random(100) < 3) {
      blinkNow = true;
      blinkUntil = millis() + 120;
  }
  if (blinkNow && millis() > blinkUntil) {
      blinkNow = false;
  }
  // ----------------------------

  if (isRinging)
      playBirdSong();

  if (millis() - lastDraw > 50) {
    canvas.fillScreen(ST7735_BLACK);
    switch (currentMode) {
      case MODE_CLOCK: drawClock(); break;
      case MODE_MENU: drawMenu(); break;
      case MODE_SET_TIME: drawSetTime(); break;
      case MODE_SET_ALARM: drawSetAlarm(); break;
      case MODE_QUOTES: drawQuotesScreen(); break;
      case MODE_RING:
        canvas.fillScreen((millis()/200)%2 ? RGB(100,0,0) : ST7735_BLACK);
        canvas.setTextColor(ST7735_WHITE); drawCenteredText(50, "ПОДЪЁМ!", FONT_TIME);
        break;
    }

    if (millis() - popupTimer < 1500 && popupMsg != "") {
      canvas.fillRoundRect(20, 30, 120, 24, 6, RGB(50,50,90));
      canvas.drawRoundRect(20, 30, 120, 24, 6, ST7735_WHITE);
      canvas.setTextColor(ST7735_WHITE); drawCenteredText(46, popupMsg, FONT_TEXT);
    }

    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), TFT_WIDTH, TFT_HEIGHT);
    lastDraw = millis();
  }


  if (millis() - lastSensor > 3000) {
      temp = dht.readTemperature();
      hum = dht.readHumidity();
      updateThemeByEnv();
      lastSensor = millis();
  }

  if (!isIdle)
      idleMode = IDLE_EYES;
}
