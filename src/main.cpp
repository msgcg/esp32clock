#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h"

// --- Таймеры и состояния ---
unsigned long lastActivity = 0;
bool isIdle = false;
unsigned long lastKeyTime = 0;

#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"
#include "fonts/FontsRus/FreeSans9.h"

#define FONT_TIME   &FreeSansBold14pt8b
#define FONT_HEADER &FreeSansBold9pt8b
#define FONT_TEXT   &FreeSans9pt8b

#define TFT_WIDTH  160
#define TFT_HEIGHT 80
#define IDLE_TIMEOUT 15000
#define MAX_PARTICLES 20

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
struct Particle { int x, y, speed; uint8_t size; bool active; uint16_t color; };

SoftClock rtc = {12, 0, 0, 1, 0};
SingleAlarm singleAlarm = {7, 0, false};
SoftClock tempRtc = {12, 0, 0, 1, 0};
SingleAlarm tempAlarm = {7, 0, false};
int menuCursor = 0;
int settingField = 0; // 0-часы, 1-минуты, 2-статус
float temp = 22.0, hum = 45.0;
bool lampState = false;
Particle particles[MAX_PARTICLES];
unsigned long lastDraw = 0, lastSensor = 0, popupTimer = 0;
String popupMsg = "";
long nextBlink = 0;
int pupilX = 0, pupilY = 0;

enum EyeEmotion { EYE_NORMAL, EYE_HAPPY, EYE_SLEEPY, EYE_ANGRY, EYE_CRY, EYE_LAUGH, EYE_WINK };
EyeEmotion eyeEmotion = EYE_NORMAL;

const char* quotes[] = {
  "Улыбнись!", "Удачи!", "Верь!", "Привет!",
  "Сможешь!", "Действуй!", "Сияй!", "Успеха!",
  "Отдохни", "Мечтай!", "Вперёд!", "Твой день!"
};
int quoteIdx = 0;
unsigned long soundTimer = 0;
bool isRinging = false;
int ringStep = 0;

String utf8rus(String source) {
  String target = "";
  int len = source.length();
  for (int i = 0; i < len; i++) {
    unsigned char n = source[i];
    if (n < 127) target += (char)n; 
    else if (n == 0xD0 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x81) target += (char)0xC0;
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2);
    }
    else if (n == 0xD1 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x91) target += (char)0xC1;
      else if (n2 >= 0x80 && n2 <= 0x8F) target += (char)(n2);
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2 - 0x10);
    }
  }
  return target;
}

void showPopup(String msg) { popupMsg = msg; popupTimer = millis(); }

void toggleLamp() {
  lampState = !lampState;
  digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
  showPopup(lampState ? "Свет ВКЛ" : "Свет ВЫКЛ");
}

void updateTime() {
  unsigned long now = millis();
  if (now - rtc.lastMillis >= 1000) {
    rtc.second++; rtc.lastMillis = now;
    if (rtc.second >= 60) { rtc.second = 0; rtc.minute++; }
    if (rtc.minute >= 60) { rtc.minute = 0; rtc.hour++; }
    if (rtc.hour >= 24) { rtc.hour = 0; rtc.month++; if(rtc.month > 12) rtc.month = 1; }
  }
}

void updateThemeAndSensors() {
  if (millis() - lastSensor > 2000) {
    float t = dht.readTemperature(); float h = dht.readHumidity();
    if(!isnan(t)) temp = t; if(!isnan(h)) hum = h;
    lastSensor = millis();
    if (rtc.hour >= 22 || rtc.hour < 6) { currentTheme = &thNight; eyeEmotion = EYE_SLEEPY; }
    else {
        if (rtc.month == 12 || rtc.month <= 2) currentTheme = &thWinter;
        else if (rtc.month >= 3 && rtc.month <= 5) currentTheme = &thSpring;
        else if (rtc.month >= 6 && rtc.month <= 8) currentTheme = &thSummer;
        else currentTheme = &thAutumn;
        eyeEmotion = EYE_NORMAL;
    }
  }
}

void updateWeatherEffect() {
  if (currentMode != MODE_CLOCK && currentMode != MODE_QUOTES && !isIdle) return;
  for(int i=0; i<MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 8) {
      particles[i].active = true; particles[i].x = random(0, TFT_WIDTH); particles[i].y = random(-20, 0);
      particles[i].speed = random(2, 4); particles[i].size = 1; particles[i].color = ST7735_CYAN;
    }
    if (particles[i].active) {
      particles[i].y += particles[i].speed;
      canvas.drawPixel(particles[i].x, particles[i].y, particles[i].color);
      if (particles[i].y > TFT_HEIGHT + 10) particles[i].active = false;
    }
  }
}

void checkAlarm() {
  if (rtc.second != 0 || !singleAlarm.active || isRinging) return;
  if (singleAlarm.hour == rtc.hour && singleAlarm.minute == rtc.minute) {
    currentMode = MODE_RING; isRinging = true; lampState = true;
    digitalWrite(PIN_RELAY, HIGH); singleAlarm.active = false;
  }
}

// Агрессивная трель для пробуждения
void playBirdSong() {
  if (!isRinging) return;
  unsigned long now = millis();
  if (now - soundTimer > 100) { // Быстрая смена нот
    soundTimer = now;
    noTone(PIN_BUZZER);
    // Высокие частоты (3-4 кГц) наиболее слышимы для пьезо
    int note = random(3000, 4500); 
    tone(PIN_BUZZER, note, 90); 
  }
}

void drawGradient() {
  uint16_t c1 = currentTheme->top, c2 = currentTheme->bot;
  for (int y = 0; y < TFT_HEIGHT; y++) {
    uint8_t r = ((c1>>11)&0x1F) + (((c2>>11)&0x1F) - ((c1>>11)&0x1F)) * y / TFT_HEIGHT;
    uint8_t g = ((c1>>5)&0x3F) + (((c2>>5)&0x3F) - ((c1>>5)&0x3F)) * y / TFT_HEIGHT;
    uint8_t b = (c1&0x1F) + ((c2&0x1F) - (c1&0x1F)) * y / TFT_HEIGHT;
    canvas.drawFastHLine(0, y, TFT_WIDTH, (r << 11) | (g << 5) | b);
  }
}

// Хелпер для центрирования текста
void drawCenteredText(int y, String text, const GFXfont* font) {
  int16_t x1, y1;
  uint16_t w, h;
  canvas.setFont(font);
  canvas.getTextBounds(utf8rus(text), 0, y, &x1, &y1, &w, &h);
  int x = (TFT_WIDTH - w) / 2;
  canvas.setCursor(x, y);
  canvas.print(utf8rus(text));
}

// Хелпер для отрисовки "iOS" элемента
void drawIOSElement(int x, int y, int w, int h, String text, bool selected) {
  // Фон элемента
  canvas.fillRoundRect(x, y, w, h, 6, currentTheme->panel);
  
  // Рамка элемента (IOS style)
  if (selected) {
    // Жирная белая/акцентная рамка для активного
    canvas.drawRoundRect(x, y, w, h, 6, ST7735_WHITE);
    canvas.drawRoundRect(x+1, y+1, w-2, h-2, 5, ST7735_WHITE);
    canvas.setTextColor(currentTheme->accent);
  } else {
    // Тонкая темная рамка для неактивного
    canvas.drawRoundRect(x, y, w, h, 6, currentTheme->border);
    canvas.setTextColor(currentTheme->text);
  }
  
  // Центрирование текста внутри плашки
  int16_t bx, by; uint16_t bw, bh;
  canvas.setFont(FONT_TEXT);
  canvas.getTextBounds(utf8rus(text), 0, 0, &bx, &by, &bw, &bh);
  
  // Корректировка вертикали для шрифта (baseline)
  int textY = y + (h/2) + (bh/2) - 2; 
  int textX = x + (w - bw) / 2;
  
  canvas.setCursor(textX, textY);
  canvas.print(utf8rus(text));
}

void drawEyes() {
  int x1 = 45, y1 = 42, x2 = 115, y2 = 42, r = 14;
  if (millis() > nextBlink && millis() < nextBlink + 120) {
    canvas.drawLine(x1-15, y1, x1+15, y1, ST7735_WHITE); canvas.drawLine(x2-15, y2, x2+15, y2, ST7735_WHITE);
  } else {
    if (millis() > nextBlink + 120) nextBlink = millis() + random(3000, 6000);
    canvas.fillCircle(x1, y1, r, ST7735_WHITE); canvas.fillCircle(x2, y2, r, ST7735_WHITE);
    canvas.fillCircle(x1, y1, 5, ST7735_BLACK); canvas.fillCircle(x2, y2, 5, ST7735_BLACK);
  }
}

void drawClock() {
  drawGradient();
  if (isIdle) {
    drawEyes();
    if ((millis() / 8000) % 2 == 0) {
      canvas.setTextColor(currentTheme->accent);
      drawCenteredText(30, quotes[quoteIdx], FONT_TEXT);
    }
  } else {
    canvas.setTextColor(currentTheme->accent);
    char buf[6]; snprintf(buf, 6, "%02d:%02d", rtc.hour, rtc.minute);
    drawCenteredText(50, buf, FONT_TIME);
    
    canvas.setTextColor(currentTheme->text);
    drawCenteredText(75, String((int)temp) + "C " + String((int)hum) + "%", FONT_TEXT);
    
    if (singleAlarm.active) canvas.fillCircle(10, 10, 3, RGB(255, 50, 50));
    if (lampState) canvas.fillCircle(150, 10, 4, RGB(255, 220, 100));
  }
  updateWeatherEffect();
}

void drawMenu() {
  drawGradient(); 
  canvas.setTextColor(currentTheme->accent);
  drawCenteredText(18, "МЕНЮ", FONT_HEADER);
  
  const char* items[] = {"Время", "Будильник", "Свет", "Цитата"};
  int itemW = 70; // Ширина кнопок
  int itemH = 22; // Высота кнопок
  int startY = 28;
  
  // Сетка 2x2 для центрирования
  for(int i = 0; i < 4; i++) {
    int col = i % 2;
    int row = i / 2;
    int x = 8 + col * (itemW + 6);
    int y = startY + row * (itemH + 4);
    drawIOSElement(x, y, itemW, itemH, items[i], i == menuCursor);
  }
}

void drawSetTime() {
  drawGradient(); 
  canvas.setTextColor(currentTheme->accent);
  drawCenteredText(18, "ВРЕМЯ", FONT_HEADER);
  
  int boxSize = 40;
  int startX = (TFT_WIDTH - (boxSize*2 + 10)) / 2; // Центрируем весь блок
  
  String hStr = tempRtc.hour < 10 ? "0"+String(tempRtc.hour) : String(tempRtc.hour);
  drawIOSElement(startX, 35, boxSize, 35, hStr, settingField == 0);
  
  canvas.setFont(FONT_TIME); canvas.setTextColor(ST7735_WHITE);
  canvas.setCursor(startX + boxSize + 2, 60); canvas.print(":"); // Двоеточие между
  
  String mStr = tempRtc.minute < 10 ? "0"+String(tempRtc.minute) : String(tempRtc.minute);
  drawIOSElement(startX + boxSize + 10, 35, boxSize, 35, mStr, settingField == 1);
}

void drawSetAlarm() {
  drawGradient(); 
  canvas.setTextColor(currentTheme->accent);
  drawCenteredText(18, "БУДИЛЬНИК", FONT_HEADER);
  
  int boxW = 35;
  int startX = 15;
  
  // Часы
  String hStr = tempAlarm.hour < 10 ? "0"+String(tempAlarm.hour) : String(tempAlarm.hour);
  drawIOSElement(startX, 35, boxW, 30, hStr, settingField == 0);
  
  // Двоеточие
  canvas.setFont(FONT_TEXT); canvas.setTextColor(ST7735_WHITE);
  canvas.setCursor(startX + boxW + 2, 55); canvas.print(":");
  
  // Минуты
  String mStr = tempAlarm.minute < 10 ? "0"+String(tempAlarm.minute) : String(tempAlarm.minute);
  drawIOSElement(startX + boxW + 8, 35, boxW, 30, mStr, settingField == 1);
  
  // Статус ВКЛ/ВЫКЛ
  drawIOSElement(startX + boxW*2 + 20, 35, 55, 30, tempAlarm.active ? "ВКЛ" : "ВЫКЛ", settingField == 2);
}

void drawQuotesScreen() {
  drawGradient(); 
  // Рисуем большую рамку для цитаты
  canvas.drawRoundRect(10, 25, TFT_WIDTH-20, 40, 6, currentTheme->border);
  canvas.fillRoundRect(11, 26, TFT_WIDTH-22, 38, 6, currentTheme->panel);
  
  canvas.setTextColor(currentTheme->accent);
  drawCenteredText(50, quotes[quoteIdx], FONT_TEXT);
}

void drawRing() {
  canvas.fillScreen((millis()/200)%2 ? RGB(100,20,20) : ST7735_BLACK); // Мигание фона красным
  canvas.setTextColor(ST7735_WHITE);
  drawCenteredText(50, "ПОДЪЁМ!", FONT_TIME);
}

void handleIR() {
  if (!irrecv.decode(&results)) return;
  unsigned long key = results.value; unsigned long now = millis();
  if (now - lastKeyTime < 150) { irrecv.resume(); return; }
  lastKeyTime = now;
  
  // ЕСЛИ БУДИЛЬНИК ЗВОНИТ - ЛЮБАЯ КНОПКА ЕГО ВЫКЛЮЧАЕТ ПОЛНОСТЬЮ
  if (currentMode == MODE_RING) {
    isRinging = false; 
    noTone(PIN_BUZZER);         // Выключить звук
    digitalWrite(PIN_RELAY, LOW); // Выключить свет
    lampState = false;          // Сбросить состояние лампы
    currentMode = MODE_CLOCK; 
    irrecv.resume(); 
    return;
  }
  
  if (isIdle) { isIdle = false; lastActivity = now; irrecv.resume(); return; }
  lastActivity = now;

  // ГЛОБАЛЬНЫЕ КНОПКИ
  if (key == IR_BTN_RETURN || key == IR_BTN_CLOCK) { currentMode = MODE_CLOCK; irrecv.resume(); return; }
  if (key == IR_BTN_RESET) { singleAlarm.active = false; showPopup("Сброс аларма"); irrecv.resume(); return; }
  if (key == IR_BTN_SUN) { toggleLamp(); irrecv.resume(); return; }

  switch (currentMode) {
    case MODE_CLOCK:
      if (key == IR_BTN_EQ) { currentMode = MODE_MENU; menuCursor = 0; }
      if (key == IR_BTN_MESSAGE) { currentMode = MODE_QUOTES; quoteIdx = random(12); }
      break;
    case MODE_MENU:
      // Перемещение по сетке 2x2
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) menuCursor = (menuCursor + 3) % 4;
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) menuCursor = (menuCursor + 1) % 4;
      if (key == IR_BTN_OK) {
        if(menuCursor==0) { currentMode = MODE_SET_TIME; tempRtc = rtc; settingField = 0; }
        if(menuCursor==1) { currentMode = MODE_SET_ALARM; tempAlarm = singleAlarm; settingField = 0; }
        if(menuCursor==2) toggleLamp();
        if(menuCursor==3) { currentMode = MODE_QUOTES; quoteIdx = random(12); }
      }
      break;
    case MODE_SET_TIME:
      if (key == IR_BTN_PREV) settingField = 0;
      if (key == IR_BTN_NEXT) settingField = 1;
      if (key == IR_BTN_PLUS) {
        if(settingField==0) tempRtc.hour = (tempRtc.hour+1)%24; else tempRtc.minute = (tempRtc.minute+1)%60;
      }
      if (key == IR_BTN_MINUS) {
        if(settingField==0) tempRtc.hour = (tempRtc.hour+23)%24; else tempRtc.minute = (tempRtc.minute+59)%60;
      }
      if (key == IR_BTN_OK) { rtc = tempRtc; currentMode = MODE_CLOCK; showPopup("Готово"); }
      break;
    case MODE_SET_ALARM:
      if (key == IR_BTN_PREV) settingField = (settingField + 2) % 3;
      if (key == IR_BTN_NEXT) settingField = (settingField + 1) % 3;
      if (key == IR_BTN_PLUS || key == IR_BTN_MINUS) {
        if(settingField==0) tempAlarm.hour = (tempAlarm.hour + (key==IR_BTN_PLUS?1:23)) % 24;
        else if(settingField==1) tempAlarm.minute = (tempAlarm.minute + (key==IR_BTN_PLUS?1:59)) % 60;
        else tempAlarm.active = !tempAlarm.active;
      }
      if (key == IR_BTN_OK) { singleAlarm = tempAlarm; currentMode = MODE_CLOCK; showPopup("ОК"); }
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
  updateTime(); updateThemeAndSensors(); checkAlarm(); handleIR();
  if (currentMode == MODE_CLOCK && !isRinging && millis() - lastActivity > IDLE_TIMEOUT) isIdle = true;
  if (isRinging) playBirdSong();

  if (millis() - lastDraw > 50) {
    canvas.fillScreen(ST7735_BLACK); canvas.setFont(NULL);
    switch (currentMode) {
      case MODE_CLOCK: drawClock(); break;
      case MODE_MENU: drawMenu(); break;
      case MODE_SET_TIME: drawSetTime(); break;
      case MODE_SET_ALARM: drawSetAlarm(); break;
      case MODE_QUOTES: drawQuotesScreen(); break;
      case MODE_RING: drawRing(); break;
    }
    // Попап уведомления
    if (millis() - popupTimer < 1500 && popupMsg != "") {
      // Рисуем попап в стиле iOS
      int pW = 120, pH = 24;
      int pX = (TFT_WIDTH - pW) / 2;
      int pY = 30;
      canvas.fillRoundRect(pX, pY, pW, pH, 6, RGB(50,50,90));
      canvas.drawRoundRect(pX, pY, pW, pH, 6, ST7735_WHITE);
      canvas.setTextColor(ST7735_WHITE);
      drawCenteredText(pY + 16, popupMsg, FONT_TEXT);
    }
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), TFT_WIDTH, TFT_HEIGHT);
    lastDraw = millis();
  }
}