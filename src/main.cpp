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
unsigned long digitInputTime = 0;
int digitBuffer = -1;
#define DIGIT_TIMEOUT 1500

// --- Шрифты с поддержкой кириллицы (FontsRus) ---
#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"
#include "fonts/FontsRus/FreeSans9.h"

#define FONT_TIME   &FreeSansBold14pt8b
#define FONT_HEADER &FreeSansBold9pt8b
#define FONT_TEXT   &FreeSans9pt8b

// --- Константы ---
#define TFT_WIDTH  160
#define TFT_HEIGHT 80
#define IDLE_TIMEOUT 15000
#define MAX_PARTICLES 20

// --- Объекты ---
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);
GFXcanvas16 canvas(TFT_WIDTH, TFT_HEIGHT);

// --- Цвета ---
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

struct Theme {
  uint16_t top, bot, text, accent, panel, border;
};

Theme thNight  = { RGB(10,15,30),  RGB(5,8,15),   RGB(220,230,255), RGB(255,220,100), RGB(30,40,60),  RGB(80,100,150) };
Theme thWinter = { RGB(40,60,100), RGB(20,30,60), RGB(230,240,255), RGB(150,220,255), RGB(40,50,80),  RGB(120,150,200) };
Theme thSpring = { RGB(30,60,30),  RGB(15,35,15), RGB(220,255,220), RGB(120,240,120), RGB(35,55,35),  RGB(100,180,100) };
Theme thSummer = { RGB(20,50,100), RGB(10,30,70), RGB(240,255,255), RGB(255,200,80),  RGB(25,45,85),  RGB(80,140,200) };
Theme thAutumn = { RGB(60,35,15),  RGB(30,20,10), RGB(255,240,220), RGB(255,160,60),  RGB(50,35,20),  RGB(150,90,40) };
Theme* currentTheme = &thNight;

// --- Режимы приложения ---
enum AppMode { MODE_CLOCK, MODE_MENU, MODE_SET_TIME, MODE_ALARM_SET, MODE_ALARM_HOUR, MODE_ALARM_MIN, MODE_ALARM_ACTIVE, MODE_QUOTES, MODE_RING };
AppMode currentMode = MODE_CLOCK;

// --- Структуры данных ---
struct SoftClock {
  int hour, minute, second;
  int month;
  unsigned long lastMillis;
};

struct Alarm {
  uint8_t hour, minute;
  bool active;
};

struct Particle { 
  int x, y, speed; 
  uint8_t size; 
  bool active; 
  uint16_t color;
};

// --- Глобальные переменные ---
SoftClock rtc = {12, 0, 0, 1, 0};
Alarm alarm = {7, 0, false};
SoftClock tempRtc = {12, 0, 0, 1, 0};
Alarm tempAlarm = {7, 0, false};
int menuCursor = 0;
int settingField = 0;
float temp = 22.0, hum = 45.0;
bool lampState = false;
Particle particles[MAX_PARTICLES];
unsigned long lastDraw = 0;
unsigned long lastSensor = 0;
unsigned long popupTimer = 0;
String popupMsg = "";
long nextBlink = 0;
int pupilX = 0, pupilY = 0;
int weatherEffect = 0; // 0=ничего, 1=дождь, 2=снег, 3=ветер/листья

enum EyeEmotion { EYE_NORMAL, EYE_HAPPY, EYE_SLEEPY, EYE_ANGRY, EYE_CRY, EYE_LAUGH, EYE_WINK };
EyeEmotion eyeEmotion = EYE_NORMAL;

const char* quotes[] = {
  "Улыбнись!", "Ты супер!", "Верь в себя!", "Доброе утро!",
  "Всё получится!", "Не сдавайся!", "Лови момент!", "Ты молодец!",
  "Отдохни", "Мечтай смелее!", "Ты справишься!", "Сегодня твой день!"
};
int quoteIdx = 0;
unsigned long soundTimer = 0;
bool isRinging = false;

// --- Правильная функция перекодировки для шрифтов FontsRus ---
String utf8rus(String source) {
  String target = "";
  int len = source.length();
  for (int i = 0; i < len; i++) {
    unsigned char n = source[i];
    if (n < 127) { 
      target += (char)n; 
    }
    else if (n == 0xD0 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x81) target += (char)0xC0;  // Ё
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2);  // А-Я
    }
    else if (n == 0xD1 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x91) target += (char)0xC1;  // ё
      else if (n2 >= 0x80 && n2 <= 0x8F) target += (char)(n2);  // а-п
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2 - 0x10);  // р-я
    }
  }
  return target;
}

// --- Получение цифры из кода кнопки ---
int getDigitFromKey(unsigned long key) {
  switch(key) {
    case IR_BTN_0: return 0;
    case IR_BTN_1: return 1;
    case IR_BTN_2: return 2;
    case IR_BTN_3: return 3;
    case IR_BTN_4: return 4;
    case IR_BTN_5: return 5;
    case IR_BTN_6: return 6;
    case IR_BTN_7: return 7;
    case IR_BTN_8: return 8;
    case IR_BTN_9: return 9;
    default: return -1;
  }
}

// --- Всплывающее сообщение ---
void showPopup(String msg) {
  popupMsg = msg;
  popupTimer = millis();
}

// --- Переключение света ---
void toggleLamp() {
  lampState = !lampState;
  digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
  showPopup(lampState ? "Свет включён" : "Свет выключен");
}

// --- Обновление времени ---
void updateTime() {
  unsigned long now = millis();
  if (now - rtc.lastMillis >= 1000) {
    rtc.second++;
    rtc.lastMillis = now;
    
    if (rtc.second >= 60) { 
      rtc.second = 0; 
      rtc.minute++; 
    }
    if (rtc.minute >= 60) { 
      rtc.minute = 0; 
      rtc.hour++; 
    }
    if (rtc.hour >= 24) {
      rtc.hour = 0; 
      rtc.month++; 
      if(rtc.month > 12) rtc.month = 1;
    }
  }
}

// --- Обновление темы и датчиков ---
void updateThemeAndSensors() {
  if (millis() - lastSensor > 2000) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if(!isnan(t)) temp = t;
    if(!isnan(h)) hum = h;
    lastSensor = millis();
    
    // Выбор темы по времени и сезону
    if (rtc.hour >= 22 || rtc.hour < 6) {
      currentTheme = &thNight;
      eyeEmotion = EYE_SLEEPY;
    } else if (rtc.hour >= 6 && rtc.hour < 10) {
      eyeEmotion = EYE_HAPPY;
    } else if (rtc.hour >= 18 && rtc.hour < 22) {
      eyeEmotion = EYE_NORMAL;
    } else {
      if (temp < 5 || (rtc.month == 12 || rtc.month <= 2)) {
        currentTheme = &thWinter;
        weatherEffect = (temp < 0) ? 2 : 1;
        eyeEmotion = EYE_NORMAL;
      } else if (rtc.month >= 3 && rtc.month <= 5) {
        currentTheme = &thSpring;
        weatherEffect = (hum > 70) ? 1 : 0;
        eyeEmotion = EYE_HAPPY;
      } else if (rtc.month >= 6 && rtc.month <= 8) {
        currentTheme = &thSummer;
        weatherEffect = (hum > 80) ? 3 : 0;
        eyeEmotion = (temp > 30) ? EYE_ANGRY : EYE_HAPPY;
      } else {
        currentTheme = &thAutumn;
        weatherEffect = (hum > 65) ? 1 : 0;
        eyeEmotion = EYE_NORMAL;
      }
    }
    
    // Эмоции по погоде
    if (hum > 85 && temp > 15) eyeEmotion = EYE_CRY;
    if (temp > 32) eyeEmotion = EYE_ANGRY;
    if (rtc.hour >= 12 && rtc.hour <= 14 && temp < 28) eyeEmotion = EYE_LAUGH;
    if (random(100) < 2) eyeEmotion = EYE_WINK; // Редкое подмигивание
  }
}

// --- Погодные эффекты ---
void updateWeatherEffect() {
  if (currentMode != MODE_CLOCK && currentMode != MODE_QUOTES && !isIdle) return;
  
  static int lastEffect = -1;
  if (weatherEffect != lastEffect) {
    for(int i=0; i<MAX_PARTICLES; i++) particles[i].active = false;
    lastEffect = weatherEffect;
  }
  
  // Активация новых частиц
  for(int i=0; i<MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 8) {
      particles[i].active = true;
      particles[i].x = random(0, TFT_WIDTH);
      particles[i].y = random(-20, 0);
      
      if (weatherEffect == 2) { // Снег
        particles[i].speed = random(1, 2);
        particles[i].size = random(1, 2);
        particles[i].color = ST7735_WHITE;
      } else if (weatherEffect == 1) { // Дождь
        particles[i].speed = random(3, 5);
        particles[i].size = 1;
        particles[i].color = ST7735_CYAN;
      } else if (weatherEffect == 3) { // Листья
        particles[i].speed = random(2, 4);
        particles[i].size = random(1, 2);
        particles[i].color = RGB(180, 120, 40);
      }
    }
    
    // Обновление и отрисовка
    if (particles[i].active) {
      particles[i].y += particles[i].speed;
      
      if (weatherEffect == 2) { // Снег
        canvas.fillCircle(particles[i].x, particles[i].y, particles[i].size, particles[i].color);
      } else if (weatherEffect == 1) { // Дождь
        canvas.drawFastVLine(particles[i].x, particles[i].y, 4, particles[i].color);
      } else if (weatherEffect == 3) { // Листья
        canvas.drawPixel(particles[i].x, particles[i].y, particles[i].color);
      }
      
      if (particles[i].y > TFT_HEIGHT + 10) particles[i].active = false;
    }
  }
}

// --- Проверка будильника ---
void checkAlarm() {
  if (rtc.second != 0 || !alarm.active || isRinging) return;
  
  if (alarm.hour == rtc.hour && alarm.minute == rtc.minute) {
    currentMode = MODE_RING;
    isRinging = true;
    lampState = true;
    digitalWrite(PIN_RELAY, HIGH);
    alarm.active = false; // Одноразовый будильник
  }
}

// --- Звук будильника (трель соловья) ---
void playBirdSong() {
  if (!isRinging) return;
  
  unsigned long now = millis();
  if (now - soundTimer > 180) {
    soundTimer = now;
    
    noTone(PIN_BUZZER);
    
    // Трель соловья - случайные высокие ноты
    int notes[] = {2600, 3200, 2800, 3500, 3000, 3300};
    int note = notes[random(0, 6)];
    int duration = random(60, 90);
    
    tone(PIN_BUZZER, note, duration);
  }
}

// --- Градиентный фон ---
void drawGradient() {
  uint8_t r1 = (currentTheme->top >> 11) & 0x1F;
  uint8_t g1 = (currentTheme->top >> 5) & 0x3F;
  uint8_t b1 = currentTheme->top & 0x1F;
  
  uint8_t r2 = (currentTheme->bot >> 11) & 0x1F;
  uint8_t g2 = (currentTheme->bot >> 5) & 0x3F;
  uint8_t b2 = currentTheme->bot & 0x1F;
  
  for (int y = 0; y < TFT_HEIGHT; y++) {
    uint8_t r = r1 + (r2 - r1) * y / TFT_HEIGHT;
    uint8_t g = g1 + (g2 - g1) * y / TFT_HEIGHT;
    uint8_t b = b1 + (b2 - b1) * y / TFT_HEIGHT;
    uint16_t color = (r << 11) | (g << 5) | b;
    canvas.drawFastHLine(0, y, TFT_WIDTH, color);
  }
}

// --- Глазки с эмоциями ---
void drawEyes() {
  int x1 = 45, y1 = 42;
  int x2 = 115, y2 = 42;
  int r = 14;
  
  // Мигание
  if (millis() > nextBlink && millis() < nextBlink + 120) {
    canvas.drawLine(x1-15, y1, x1+15, y1, ST7735_WHITE);
    canvas.drawLine(x2-15, y2, x2+15, y2, ST7735_WHITE);
    return;
  } else if (millis() > nextBlink + 120) {
    nextBlink = millis() + random(3000, 6000);
  }
  
  // Движение зрачков
  static unsigned long lastMove = 0;
  if (millis() - lastMove > 2000) {
    pupilX = random(-4, 5);
    pupilY = random(-3, 4);
    lastMove = millis();
  }
  
  // Глаза
  canvas.fillCircle(x1, y1, r, ST7735_WHITE);
  canvas.fillCircle(x2, y2, r, ST7735_WHITE);
  
  // Зрачки
  canvas.fillCircle(x1 + pupilX, y1 + pupilY, 5, ST7735_BLACK);
  canvas.fillCircle(x2 + pupilX, y2 + pupilY, 5, ST7735_BLACK);
  
  // Эмоции
  switch(eyeEmotion) {
    case EYE_HAPPY: // Улыбающиеся глаза
      canvas.drawLine(x1-8, y1+8, x1, y1+11, ST7735_BLACK);
      canvas.drawLine(x1, y1+11, x1+8, y1+8, ST7735_BLACK);
      canvas.drawLine(x2-8, y2+8, x2, y2+11, ST7735_BLACK);
      canvas.drawLine(x2, y2+11, x2+8, y2+8, ST7735_BLACK);
      break;
      
    case EYE_SLEEPY: // Полузакрытые
      canvas.fillRect(x1-15, y1-3, 30, 6, currentTheme->top);
      canvas.fillRect(x2-15, y2-3, 30, 6, currentTheme->top);
      break;
      
    case EYE_ANGRY: // Брови
      canvas.drawLine(x1-12, y1-12, x1-3, y1-6, ST7735_BLACK);
      canvas.drawLine(x1+3, y1-6, x1+12, y1-12, ST7735_BLACK);
      canvas.drawLine(x2-12, y2-12, x2-3, y2-6, ST7735_BLACK);
      canvas.drawLine(x2+3, y2-6, x2+12, y2-12, ST7735_BLACK);
      break;
      
    case EYE_CRY: // Слёзы
      canvas.fillCircle(x1+8, y1+12, 2, ST7735_BLUE);
      canvas.fillCircle(x2-8, y2+12, 2, ST7735_BLUE);
      canvas.drawLine(x1+8, y1+14, x1+8, y1+18, ST7735_BLUE);
      canvas.drawLine(x2-8, y2+14, x2-8, y2+18, ST7735_BLUE);
      break;
      
    case EYE_LAUGH: // Смеющиеся
      canvas.drawLine(x1-10, y1, x1+10, y1, ST7735_BLACK);
      canvas.drawLine(x2-10, y2, x2+10, y2, ST7735_BLACK);
      canvas.fillCircle(x1-12, y1+8, 4, RGB(255, 150, 150));
      canvas.fillCircle(x2+12, y2+8, 4, RGB(255, 150, 150));
      break;
      
    case EYE_WINK: // Подмигивание
      canvas.drawLine(x1-15, y1, x1+15, y1, ST7735_WHITE);
      break;
      
    default: break;
  }
}

// --- Элемент списка меню ---
void drawMenuItem(int y, String text, bool selected) {
  if (selected) {
    canvas.fillRoundRect(5, y, TFT_WIDTH-10, 18, 4, currentTheme->panel);
    canvas.drawRoundRect(5, y, TFT_WIDTH-10, 18, 4, currentTheme->border);
    canvas.setTextColor(currentTheme->accent);
  } else {
    canvas.setTextColor(currentTheme->text);
  }
  canvas.setCursor(15, y + 14);
  canvas.print(utf8rus(text));
}

// --- Главный экран ---
void drawClock() {
  drawGradient();
  
  if (isIdle) {
    // Режим сна: глазки + цитата
    drawEyes();
    
    if ((millis() / 8000) % 2 == 0) {
      canvas.fillRoundRect(12, 8, TFT_WIDTH-24, 28, 6, currentTheme->panel);
      canvas.drawRoundRect(12, 8, TFT_WIDTH-24, 28, 6, currentTheme->border);
      canvas.setFont(FONT_TEXT);
      canvas.setTextColor(currentTheme->accent);
      canvas.setCursor(20, 25);
      canvas.print(utf8rus(quotes[quoteIdx]));
    }
  } else {
    // Часы по центру
    canvas.setFont(FONT_TIME);
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", rtc.hour, rtc.minute);
    
    int16_t x1, y1, w, h;
    canvas.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor((TFT_WIDTH - w) / 2, 48);
    canvas.print(timeStr);
    
    // Погода внизу
    canvas.setFont(FONT_TEXT);
    canvas.setTextColor(currentTheme->text);
    String weather = String((int)temp) + "C  " + String((int)hum) + "%";
    canvas.getTextBounds(utf8rus(weather), 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((TFT_WIDTH - w) / 2, 75);
    canvas.print(utf8rus(weather));
    
    // Индикатор будильника
    if (alarm.active) {
      canvas.fillCircle(10, 10, 3, RGB(255, 100, 100));
    }
    // Индикатор света
    if (lampState) {
      canvas.fillCircle(150, 10, 4, RGB(255, 220, 100));
    }
  }
  
  updateWeatherEffect();
}

// --- Меню ---
void drawMenu() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 60) / 2, 18);
  canvas.print(utf8rus("МЕНЮ"));
  
  canvas.setFont(FONT_TEXT);
  const char* items[] = {"Установить время", "Настроить будильник", "Свет", "Цитата"};
  int total = 4;
  
  int start = (menuCursor > 1) ? menuCursor - 1 : 0;
  if (start > total - 3) start = total - 3;
  
  int y = 30;
  for(int i = 0; i < 3 && start + i < total; i++) {
    drawMenuItem(y, items[start + i], start + i == menuCursor);
    y += 22;
  }
}

// --- Установка времени ---
void drawSetTime() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 110) / 2, 15);
  canvas.print(utf8rus("ВРЕМЯ"));
  
  canvas.setFont(FONT_TEXT);
  
  const char* labels[] = {"Часы", "Минуты"};
  int values[] = {tempRtc.hour, tempRtc.minute};
  
  int y = 38;
  for(int i=0; i<2; i++) {
    canvas.setTextColor(currentTheme->text);
    canvas.setCursor(15, y + 12);
    canvas.print(utf8rus(labels[i]));
    canvas.print(": ");
    
    canvas.setTextColor((i == settingField) ? currentTheme->accent : currentTheme->text);
    if (values[i] < 10) canvas.print("0");
    canvas.print(values[i]);
    
    y += 22;
  }
  
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(15, 75);
  canvas.print(utf8rus("Стрелки: выбор  +/-: изменить  ОК: сохранить"));
}

// --- Подменю будильника: часы ---
void drawAlarmHour() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 120) / 2, 20);
  canvas.print(utf8rus("БУДИЛЬНИК: ЧАСЫ"));
  
  canvas.setFont(FONT_TIME);
  canvas.setTextColor(currentTheme->accent);
  char buf[3];
  snprintf(buf, sizeof(buf), "%02d", tempAlarm.hour);
  canvas.setCursor((TFT_WIDTH - 40) / 2, 50);
  canvas.print(buf);
  
  canvas.setFont(FONT_TEXT);
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(10, 75);
  canvas.print(utf8rus("PREV/NEXT: выбор  ОК: далее"));
}

// --- Подменю будильника: минуты ---
void drawAlarmMin() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 130) / 2, 20);
  canvas.print(utf8rus("БУДИЛЬНИК: МИНУТЫ"));
  
  canvas.setFont(FONT_TIME);
  canvas.setTextColor(currentTheme->accent);
  char buf[3];
  snprintf(buf, sizeof(buf), "%02d", tempAlarm.minute);
  canvas.setCursor((TFT_WIDTH - 40) / 2, 50);
  canvas.print(buf);
  
  canvas.setFont(FONT_TEXT);
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(10, 75);
  canvas.print(utf8rus("PREV/NEXT: выбор  ОК: далее"));
}

// --- Подменю будильника: активность ---
void drawAlarmActive() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 130) / 2, 20);
  canvas.print(utf8rus("БУДИЛЬНИК: СТАТУС"));
  
  canvas.setFont(FONT_TEXT);
  canvas.setTextColor(tempAlarm.active ? currentTheme->accent : currentTheme->text);
  canvas.setCursor((TFT_WIDTH - 60) / 2, 45);
  canvas.print(utf8rus(tempAlarm.active ? "ВКЛЮЧЁН" : "ВЫКЛЮЧЕН"));
  
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(10, 75);
  canvas.print(utf8rus("PREV/NEXT: переключить  ОК: сохранить"));
}

// --- Экран цитат ---
void drawQuotes() {
  drawGradient();
  
  canvas.fillRoundRect(10, 15, TFT_WIDTH-20, 45, 8, currentTheme->panel);
  canvas.drawRoundRect(10, 15, TFT_WIDTH-20, 45, 8, currentTheme->border);
  
  canvas.setFont(FONT_TEXT);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor(20, 42);
  canvas.print(utf8rus(quotes[quoteIdx]));
  
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(25, 75);
  canvas.print(utf8rus("СОЛНЦЕ: свет  ЧАСЫ: назад"));
}

// --- Экран будильника ---
void drawRing() {
  uint16_t bg = ((millis() / 300) % 2 == 0) ? RGB(40, 20, 20) : RGB(60, 30, 30);
  canvas.fillScreen(bg);
  
  canvas.setFont(FONT_TIME);
  canvas.setTextColor(ST7735_WHITE);
  canvas.setCursor(20, 45);
  canvas.print(utf8rus("ПОДЪЁМ!"));
  
  // Мигающая иконка солнца
  canvas.drawCircle(145, 20, 8, ST7735_YELLOW);
  canvas.fillCircle(145, 20, 6, (millis()/200)%2 ? ST7735_YELLOW : ST7735_BLACK);
}

// --- Обработка ИК ---
void handleIR() {
  if (!irrecv.decode(&results)) return;
  
  unsigned long key = results.value;
  unsigned long now = millis();
  
  // Антидребезг
  if (now - lastKeyTime < 150) {
    irrecv.resume();
    return;
  }
  lastKeyTime = now;
  
  int digit = getDigitFromKey(key);
  
  // Любая кнопка пробуждает из сна
  if (isIdle) {
    isIdle = false;
    lastActivity = millis();
    if (key != IR_BTN_CLOCK && key != IR_BTN_SUN) {
      quoteIdx = random(0, sizeof(quotes)/sizeof(quotes[0]));
    }
    irrecv.resume();
    return;
  }
  
  lastActivity = millis();
  
  // === ГЛОБАЛЬНЫЕ КНОПКИ (работают везде) ===
  if (key == IR_BTN_CLOCK) {
    currentMode = MODE_CLOCK;
    menuCursor = 0;
    settingField = 0;
    irrecv.resume();
    return;
  }
  
  if (key == IR_BTN_SUN) {
    toggleLamp();
    irrecv.resume();
    return;
  }
  
  // Сброс будильника любой кнопкой (кроме солнца)
  if (currentMode == MODE_RING) {
    if (key != IR_BTN_SUN) {
      isRinging = false;
      noTone(PIN_BUZZER);
      digitalWrite(PIN_RELAY, LOW);
      lampState = false;
      currentMode = MODE_CLOCK;
    }
    irrecv.resume();
    return;
  }
  
  // === Обработка режимов ===
  switch (currentMode) {
    case MODE_CLOCK:
      if (key == IR_BTN_EQ) { 
        currentMode = MODE_MENU; 
        menuCursor = 0; 
      }
      if (key == IR_BTN_MESSAGE) { 
        currentMode = MODE_QUOTES; 
        quoteIdx = random(0, sizeof(quotes)/sizeof(quotes[0])); 
      }
      if (key == IR_BTN_RESET) {
        alarm.active = false;
        showPopup("Будильник сброшен");
      }
      break;
      
    case MODE_MENU:
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { 
        menuCursor--; 
        if (menuCursor < 0) menuCursor = 3; 
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { 
        menuCursor++; 
        if (menuCursor > 3) menuCursor = 0; 
      }
      if (key == IR_BTN_OK) {
        switch(menuCursor) {
          case 0: 
            currentMode = MODE_SET_TIME; 
            tempRtc = rtc; 
            settingField = 0; 
            break;
          case 1: 
            currentMode = MODE_ALARM_HOUR; 
            tempAlarm = alarm; 
            break;
          case 2: 
            toggleLamp(); 
            break;
          case 3: 
            currentMode = MODE_QUOTES; 
            quoteIdx = random(0, sizeof(quotes)/sizeof(quotes[0])); 
            break;
        }
      }
      break;
      
    case MODE_SET_TIME:
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { 
        settingField = (settingField - 1 + 2) % 2; 
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { 
        settingField = (settingField + 1) % 2; 
      }
      if (key == IR_BTN_PLUS) {
        if (settingField == 0) tempRtc.hour = (tempRtc.hour + 1) % 24;
        else tempRtc.minute = (tempRtc.minute + 1) % 60;
      }
      if (key == IR_BTN_MINUS) {
        if (settingField == 0) tempRtc.hour = (tempRtc.hour + 23) % 24;
        else tempRtc.minute = (tempRtc.minute + 59) % 60;
      }
      // Цифровой ввод
      if (digit != -1) {
        if (now - digitInputTime < DIGIT_TIMEOUT && digitBuffer != -1) {
          int value = digitBuffer * 10 + digit;
          if (settingField == 0 && value < 24) tempRtc.hour = value;
          else if (settingField == 1 && value < 60) tempRtc.minute = value;
          digitBuffer = -1;
        } else {
          digitBuffer = digit;
          digitInputTime = now;
        }
      }
      if (key == IR_BTN_OK) {
        rtc = tempRtc;
        currentMode = MODE_CLOCK;
        showPopup("Время установлено");
      }
      break;
      
    case MODE_ALARM_HOUR:
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) tempAlarm.hour = (tempAlarm.hour + 1) % 24;
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) tempAlarm.hour = (tempAlarm.hour + 23) % 24;
      if (key == IR_BTN_OK) currentMode = MODE_ALARM_MIN;
      break;
      
    case MODE_ALARM_MIN:
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) tempAlarm.minute = (tempAlarm.minute + 1) % 60;
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) tempAlarm.minute = (tempAlarm.minute + 59) % 60;
      if (key == IR_BTN_OK) currentMode = MODE_ALARM_ACTIVE;
      break;
      
    case MODE_ALARM_ACTIVE:
      if (key == IR_BTN_NEXT || key == IR_BTN_PREV || key == IR_BTN_PLUS || key == IR_BTN_MINUS) {
        tempAlarm.active = !tempAlarm.active;
      }
      if (key == IR_BTN_OK) {
        alarm = tempAlarm;
        currentMode = MODE_CLOCK;
        showPopup(alarm.active ? "Будильник включён" : "Будильник выключен");
      }
      break;
      
    case MODE_QUOTES:
      if (key == IR_BTN_MESSAGE || key == IR_BTN_NEXT || key == IR_BTN_PLUS) {
        quoteIdx = (quoteIdx + 1) % (sizeof(quotes)/sizeof(quotes[0]));
      }
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) {
        quoteIdx = (quoteIdx + sizeof(quotes)/sizeof(quotes[0]) - 1) % (sizeof(quotes)/sizeof(quotes[0]));
      }
      break;
  }
  
  irrecv.resume();
}

// --- SETUP и LOOP ---
void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  
  dht.begin();
  irrecv.enableIRIn();
  
  tft.initR(INITR_MINI160x80);
  tft.setRotation(3);
  tft.fillScreen(ST7735_BLACK);
  
  randomSeed(analogRead(0));
  
  rtc.lastMillis = millis();
  lastActivity = millis();
  lastDraw = millis();
  
  // Начальное время (можно установить через меню)
  rtc.hour = 12;
  rtc.minute = 0;
}

void loop() {
  updateTime();
  updateThemeAndSensors();
  checkAlarm();
  handleIR();
  
  // Переход в режим сна при бездействии
  if (currentMode == MODE_CLOCK && !isRinging && millis() - lastActivity > IDLE_TIMEOUT) {
    isIdle = true;
  }
  
  // Звук будильника
  if (currentMode == MODE_RING && isRinging) {
    playBirdSong();
  }
  
  // Отрисовка 20 кадров/сек
  if (millis() - lastDraw > 50) {
    canvas.fillScreen(ST7735_BLACK);
    
    switch (currentMode) {
      case MODE_CLOCK: drawClock(); break;
      case MODE_MENU: drawMenu(); break;
      case MODE_SET_TIME: drawSetTime(); break;
      case MODE_ALARM_HOUR: drawAlarmHour(); break;
      case MODE_ALARM_MIN: drawAlarmMin(); break;
      case MODE_ALARM_ACTIVE: drawAlarmActive(); break;
      case MODE_QUOTES: drawQuotes(); break;
      case MODE_RING: drawRing(); break;
    }
    
    // Всплывающее сообщение
    if (millis() - popupTimer < 2000 && popupMsg != "") {
      canvas.fillRoundRect(15, 30, TFT_WIDTH-30, 22, 5, RGB(60, 80, 120));
      canvas.drawRoundRect(15, 30, TFT_WIDTH-30, 22, 5, currentTheme->border);
      canvas.setTextColor(ST7735_WHITE);
      canvas.setFont(FONT_TEXT);
      
      int16_t x1, y1, w, h;
      canvas.getTextBounds(utf8rus(popupMsg), 0, 0, &x1, &y1, &w, &h);
      canvas.setCursor((TFT_WIDTH - w) / 2, 45);
      canvas.print(utf8rus(popupMsg));
    }
    
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), TFT_WIDTH, TFT_HEIGHT);
    lastDraw = millis();
  }
}