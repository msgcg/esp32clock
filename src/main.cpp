#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h"

// ✅ ПРАВИЛЬНЫЙ МАКРОС ДЛЯ ВАШЕГО ДИСПЛЕЯ (BGR565 + инверсия):
#define RGB(r, g, b) (uint16_t)(0xFFFF - ((((b) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((r) >> 3)))

enum IdleMode { IDLE_EYES, IDLE_QUOTE };
IdleMode idleMode = IDLE_EYES;
unsigned long lastIdleSwitch = 0;
#define IDLE_SWITCH_TIME 5000
#define HEART_COLOR RGB(227, 27, 35)  // Теперь будет ❤️ красным!
bool blinkNow = false;
unsigned long blinkUntil = 0;

int currentEyeR = 14;
int currentPupilR = 5;
int currentSpacing = 65;

enum EyeEmotion {
  E_NEUTRAL,      // 0: нейтральное
  E_HAPPY,        // 1: счастливое (щёчки + зрачки вверх)
  E_ANGRY,        // 2: злой (брови вниз, зрачки вниз)
  E_SLEEPY,       // 3: сонный (полный прищур)
  E_SURPRISE,     // 4: удивлённый (большие глаза, маленькие зрачки, брови вверх)
  E_LOOK_LEFT,    // 5: смотрит влево
  E_LOOK_RIGHT,   // 6: смотрит вправо
  E_LOOK_UP,      // 7: смотрит вверх
  E_LOOK_DOWN,    // 8: смотрит вниз
  E_SAD,          // 9: грустный (брови вверх, зрачки вниз)
  E_WINK_LEFT,    // 10: подмигивает левым глазом
  E_WINK_RIGHT,   // 11: подмигивает правым глазом
  E_IN_LOVE,      // 12: влюблённый (сердечки вместо зрачков)
  E_TIRED         // 13: уставший (лёгкий прищур)
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

#include "fonts/FontsRus/FreeSansBold18.h"
#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"
#include "fonts/FontsRus/FreeSans9.h"

#define FONT_ALARM  &FreeSansBold14pt8b
#define FONT_TIME   &FreeSansBold18pt8b
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

struct Theme { uint16_t top, bot, text, accent, panel, border; };
// ============ СВЕТЛЫЕ ДНЕВНЫЕ ТЕМЫ (9:00 - 17:59) ============
Theme thWinterLight = { 
  RGB(220, 240, 255), RGB(180, 210, 240), // Светлый голубой градиент
  RGB(40, 60, 90),    // Тёмно-синий текст (контраст!)
  RGB(100, 180, 255), // Голубой акцент
  RGB(200, 225, 250), // Светлая панель
  RGB(140, 170, 210)  // Мягкая граница
};

Theme thSpringLight = { 
  RGB(220, 255, 220), RGB(190, 240, 190), // Светлый зелёный градиент
  RGB(50, 80, 50),    // Тёмно-зелёный текст
  RGB(120, 220, 120), // Сочный зелёный акцент
  RGB(210, 245, 210),
  RGB(120, 180, 120)
};

Theme thSummerLight = { 
  RGB(200, 235, 255), RGB(170, 215, 245), // Светлый небесный градиент
  RGB(30, 50, 80),    // Тёмно-синий текст
  RGB(255, 200, 100), // Тёплый солнечный акцент
  RGB(190, 225, 250),
  RGB(110, 150, 200)
};

Theme thAutumnLight = { 
  RGB(255, 245, 220), RGB(245, 230, 200), // Светлый кремовый градиент
  RGB(70, 50, 30),    // Тёмно-коричневый текст
  RGB(255, 150, 60),  // Яркий оранжевый акцент
  RGB(250, 240, 220),
  RGB(160, 110, 70)
};

// ============ ТЁМНЫЕ НОЧНЫЕ ТЕМЫ (22:00 - 5:59) ============
Theme thWinterDark = { 
  RGB(30, 50, 80), RGB(20, 35, 60),       // Тёмно-синий градиент
  RGB(180, 220, 255), // Светлый голубой текст
  RGB(120, 190, 255), // Мягкий голубой акцент
  RGB(40, 60, 90),
  RGB(80, 110, 150)
};

Theme thSpringDark = { 
  RGB(35, 60, 35), RGB(25, 45, 25),       // Тёмно-зелёный градиент
  RGB(200, 240, 200), // Светлый зелёный текст
  RGB(130, 230, 130), // Яркий зелёный акцент
  RGB(45, 70, 45),
  RGB(100, 170, 100)
};

Theme thSummerDark = { 
  RGB(25, 55, 95), RGB(18, 40, 75),       // Тёмно-голубой градиент
  RGB(210, 245, 255), // Светлый голубой текст
  RGB(255, 210, 120), // Тёплый янтарный акцент
  RGB(35, 65, 105),
  RGB(90, 140, 190)
};

Theme thAutumnDark = { 
  RGB(65, 45, 30), RGB(45, 30, 20),       // Тёмно-коричневый градиент
  RGB(255, 230, 200), // Светлый кремовый текст
  RGB(255, 170, 80),  // Тёплый оранжевый акцент
  RGB(75, 55, 40),
  RGB(150, 100, 60)
};

// ============ СПЕЦИАЛЬНАЯ ТЁПЛАЯ НОЧНАЯ ТЕМА (без синего!) ============
Theme thNightWarm = { 
  RGB(15, 10, 10), RGB(5, 3, 3),          // Тёмно-бордовый градиент (0% синего!)
  RGB(230, 200, 170), // Кремовый текст
  RGB(255, 180, 100), // Мягкий янтарь
  RGB(25, 18, 18),
  RGB(50, 40, 40)
};

// ============ ВЕЧЕРНИЕ ТЕМЫ (18:00 - 21:59) — плавный переход ============
Theme thWinterEvening = { 
  RGB(100, 130, 170), RGB(70, 95, 140),
  RGB(200, 225, 250),
  RGB(130, 190, 255),
  RGB(110, 140, 180),
  RGB(100, 130, 170)
};

Theme thSpringEvening = { 
  RGB(100, 140, 100), RGB(75, 110, 75),
  RGB(210, 240, 210),
  RGB(140, 225, 140),
  RGB(110, 150, 110),
  RGB(100, 140, 100)
};

Theme thSummerEvening = { 
  RGB(80, 120, 170), RGB(60, 95, 145),
  RGB(220, 245, 255),
  RGB(255, 205, 130),
  RGB(90, 130, 180),
  RGB(85, 125, 175)
};

Theme thAutumnEvening = { 
  RGB(130, 100, 70), RGB(100, 75, 50),
  RGB(245, 225, 200),
  RGB(255, 165, 90),
  RGB(140, 110, 80),
  RGB(135, 105, 75)
};
Theme* currentTheme = &thSummerLight;

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
  // Позитив и настроение
  "Улыбнись!", "Хорошего дня!", "Ты супер!", "Сияй!", "Радуйся!", "Всё получится!",
  "Ты чудо!", "Прекрасный день!", "Лови момент!", "Ты топ!", "Вдохновляй!",
  
  // Забота и отдых
  "Время чая", "Отдохни", "Расслабься", "Люби", "Время для себя",
  "Цени", "Не спеши", "Всё хорошо", "Глубокий вдох", "Минутка тишины",
  
  // Поддержка и мотивация
  "Ты сможешь!", "Верь в себя!", "Только вперёд!", "Действуй!", "Маленький шаг",
  "Не сдавайся!", "Всё возможно!", "Ты справишься!",
  
  // Тепло и уют
  "Обнимаю", "С любовью", "Тепла и уюта", "Гармонии", "Радости!", "Счастья!",
  "Мира в душе", "Добрые мысли", "Чудеса рядом",
  
  // Мечты и вдохновение
  "Мечтай!", "Твори!", "Создавай!", "Живи!", "Просто будь!"
};

const int NUM_QUOTES = sizeof(quotes) / sizeof(char*);
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
  
  // 🌙 ГЛУБОКАЯ НОЧЬ (23:00 - 5:59) — тёплая тема БЕЗ синего спектра
  if (h >= 23 || h < 6) {
    currentTheme = &thNightWarm;
    return;
  }
  
  // 🌆 ВЕЧЕР (18:00 - 22:59) — тёмные, но не агрессивные тона
  if (h >= 18 && h < 23) {
    if (temp > 28) currentTheme = &thAutumnEvening;
    else if (temp < 15) currentTheme = &thWinterEvening;
    else if (hum > 70) currentTheme = &thSpringEvening;
    else if (hum < 30) currentTheme = &thSummerEvening;
    else currentTheme = &thSpringEvening;
    return;
  }
  
  // 🌅 РАННЕЕ УТРО (6:00 - 8:59) — мягкие средние тона
  if (h >= 6 && h < 9) {
    if (temp > 28) currentTheme = &thAutumnDark;
    else if (temp < 15) currentTheme = &thWinterDark;
    else if (hum > 70) currentTheme = &thSpringDark;
    else if (hum < 30) currentTheme = &thSummerDark;
    else currentTheme = &thSpringDark;
    return;
  }
  
  // ☀️ ДЕНЬ (9:00 - 17:59) — СВЕТЛЫЕ тона с тёмным текстом для контраста!
  if (h >= 9 && h < 18) {
    if (temp > 28) currentTheme = &thAutumnLight;
    else if (temp < 15) currentTheme = &thWinterLight;
    else if (hum > 70) currentTheme = &thSpringLight;
    else if (hum < 30) currentTheme = &thSummerLight;
    else currentTheme = &thSpringLight;
    return;
  }
  
  // 🌇 ПОЗДНИЙ ВЕЧЕР (18:00 - 22:59) — уже обработан выше
  currentTheme = &thSpringLight; // fallback
}

void updateTime() {
  unsigned long now = millis();
  
  // Вычисляем, сколько времени прошло с последнего зафиксированного тика
  // Благодаря unsigned long переполнение millis() через 49 дней обрабатывается корректно автоматически
  unsigned long delta = now - rtc.lastMillis;

  if (delta >= 1000) {
    // Считаем, сколько полных секунд прошло (обычно 1, но если код "зависнет", то больше)
    unsigned long secondsPassed = delta / 1000;
    
    // Сдвигаем метку времени ровно на количество прошедших секунд
    // Это важно: мы не приравниваем к now, а прибавляем 1000, чтобы не накапливать погрешность
    rtc.lastMillis += (secondsPassed * 1000);

    rtc.second += secondsPassed;

    // Стандартная обработка переполнения времени
    while (rtc.second >= 60) {
      rtc.second -= 60;
      rtc.minute++;
      alarmSkipToday = false; 
    }

    while (rtc.minute >= 60) {
      rtc.minute -= 60;
      rtc.hour++;
    }

    while (rtc.hour >= 24) {
      rtc.hour -= 24;
      rtc.month++;
      if (rtc.month > 12) rtc.month = 1;
    }
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
  bool isLight = (currentTheme == &thWinterLight || currentTheme == &thSpringLight || 
                  currentTheme == &thSummerLight || currentTheme == &thAutumnLight);
  
  for (int i = 0; i < 12; i++) {
    int x = random(TFT_WIDTH);
    int y = random(TFT_HEIGHT);
    int len = random(4, 10);
    
    if (isLight) {
      // Днём: тёмные капли на светлом фоне
      canvas.drawFastVLine(x, y, len, RGB(80, 120, 180));
    } else {
      // Ночью: светлые капли на тёмном фоне
      canvas.drawFastVLine(x, y, len, RGB(160, 200, 255));
    }
  }
}

void drawSand() {
  bool isLight = (currentTheme == &thWinterLight || currentTheme == &thSpringLight || 
                  currentTheme == &thSummerLight || currentTheme == &thAutumnLight);
  
  for (int i = 0; i < 25; i++) {
    int x = random(TFT_WIDTH);
    int y = random(TFT_HEIGHT);
    
    if (isLight) {
      // Днём: тёмные песчинки
      canvas.drawPixel(x, y, RGB(150, 130, 100));
    } else {
      // Ночью: светлые песчинки
      canvas.drawPixel(x, y, RGB(220, 200, 160));
    }
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
  int spacing = currentSpacing;
  int x1 = (TFT_WIDTH - spacing) / 2;
  int x2 = x1 + spacing;

  int eyeR = currentEyeR;
  int pupilR = currentPupilR;
  int dx = 0, dy = 0;
  bool closedLeft = blinkNow;
  bool closedRight = blinkNow;
  bool drawPupils = true;
  bool drawHighlights = true;
  bool drawCheeks = false;
  int browOffset = 0; // 0 = нейтрально, -3 = злой, +3 = грустный

  // --- ЛОГИКА ЭМОЦИЙ ---
  switch (currentEmotion) {
    case E_HAPPY:
      dy = -2;
      drawCheeks = true;
      break;
      
    case E_ANGRY:
      browOffset = -3; // брови опущены вниз
      dy = 2;          // зрачки вниз (сердитый взгляд)
      break;
      
    case E_SLEEPY:
      closedLeft = true;
      closedRight = true;
      drawPupils = false;
      drawHighlights = false;
      break;
      
    case E_SURPRISE:
      eyeR += 3;               // глаза широко раскрыты
      pupilR = max(2, pupilR - 3); // зрачки сужены от удивления
      dy = -3;
      break;
      
    case E_LOOK_LEFT:
      dx = -5;
      break;
      
    case E_LOOK_RIGHT:
      dx = 5;
      break;
      
    case E_LOOK_UP:
      dy = -6;
      break;
      
    case E_LOOK_DOWN:
      dy = 6;
      break;
      
    case E_SAD:
      browOffset = +3;  // брови приподняты (грустные)
      dy = 4;           // зрачки смотрят вниз
      break;
      
    case E_WINK_LEFT:
      closedLeft = true;
      closedRight = false;
      drawPupils = true;
      drawHighlights = true;
      dy = -2; // правый глаз "улыбается"
      break;
      
    case E_WINK_RIGHT:
      closedLeft = false;
      closedRight = true;
      drawPupils = true;
      drawHighlights = true;
      dy = -2; // левый глаз "улыбается"
      break;
      
    case E_IN_LOVE:
      // Сердечки вместо зрачков — рисуем позже отдельно
      drawPupils = false;
      drawHighlights = false;
      dy = -2;
      break;
      
    case E_TIRED:
      // Лёгкий постоянный прищур
      closedLeft = true;
      closedRight = true;
      drawPupils = true;
      drawHighlights = false;
      dy = 2;
      break;
      
    case E_NEUTRAL:
    default:
      break;
  }

  // --- ОТРИСОВКА ЛЕВОГО ГЛАЗА ---
  if (closedLeft) {
    // Прищур: полный белый круг + чёрная "крышка" сверху
    canvas.fillCircle(x1, baseY, eyeR, ST7735_WHITE);
    canvas.fillRect(x1 - eyeR, baseY - eyeR, eyeR * 2, eyeR - 1, ST7735_BLACK);
    canvas.drawFastHLine(x1 - eyeR + 2, baseY, eyeR * 2 - 4, ST7735_WHITE);
  } else {
    canvas.fillCircle(x1, baseY, eyeR, ST7735_WHITE);
    if (drawPupils) {
      canvas.fillCircle(x1 + dx, baseY + dy, pupilR, ST7735_BLACK);
      if (drawHighlights) {
        int hR = pupilR > 4 ? 2 : 1;
        canvas.fillCircle(x1 + dx + hR, baseY + dy - hR, hR, ST7735_WHITE);
      }
    }
    // Сердечко для влюблённого состояния
    if (currentEmotion == E_IN_LOVE) {
      int hSize = pupilR + 1;
      canvas.fillCircle(x1 + dx - hSize/2, baseY + dy - hSize/3, hSize/2, HEART_COLOR);
      canvas.fillCircle(x1 + dx + hSize/2, baseY + dy - hSize/3, hSize/2, HEART_COLOR);
      canvas.fillTriangle(
        x1 + dx - hSize/2, baseY + dy + hSize/4,
        x1 + dx + hSize/2, baseY + dy + hSize/4,
        x1 + dx, baseY + dy + hSize/2,
        HEART_COLOR
      );
    }
  }

  // --- ОТРИСОВКА ПРАВОГО ГЛАЗА ---
  if (closedRight) {
    canvas.fillCircle(x2, baseY, eyeR, ST7735_WHITE);
    canvas.fillRect(x2 - eyeR, baseY - eyeR, eyeR * 2, eyeR - 1, ST7735_BLACK);
    canvas.drawFastHLine(x2 - eyeR + 2, baseY, eyeR * 2 - 4, ST7735_WHITE);
  } else {
    canvas.fillCircle(x2, baseY, eyeR, ST7735_WHITE);
    if (drawPupils) {
      canvas.fillCircle(x2 + dx, baseY + dy, pupilR, ST7735_BLACK);
      if (drawHighlights) {
        int hR = pupilR > 4 ? 2 : 1;
        canvas.fillCircle(x2 + dx + hR, baseY + dy - hR, hR, ST7735_WHITE);
      }
    }
    if (currentEmotion == E_IN_LOVE) {
      int hSize = pupilR + 1;
      canvas.fillCircle(x2 + dx - hSize/2, baseY + dy - hSize/3, hSize/2, HEART_COLOR);
      canvas.fillCircle(x2 + dx + hSize/2, baseY + dy - hSize/3, hSize/2, HEART_COLOR);
      canvas.fillTriangle(
        x2 + dx - hSize/2, baseY + dy + hSize/4,
        x2 + dx + hSize/2, baseY + dy + hSize/4,
        x2 + dx, baseY + dy + hSize/2,
        HEART_COLOR
      );
    }
  }

  // --- БРОВИ (только при открытых глазах) ---
  if (!closedLeft || !closedRight) {
    int browY = baseY - eyeR - 3 + browOffset;
    
    if (currentEmotion == E_ANGRY) {
      // Толстые злые брови с выраженным наклоном
      canvas.drawLine(x1 - eyeR/2 + 1, browY + 2, x1 + eyeR/2 - 1, browY - 1, ST7735_BLACK);
      canvas.drawLine(x1 - eyeR/2, browY + 1, x1 + eyeR/2, browY - 2, ST7735_BLACK);
      canvas.drawLine(x2 + eyeR/2 - 1, browY + 2, x2 - eyeR/2 + 1, browY - 1, ST7735_BLACK);
      canvas.drawLine(x2 + eyeR/2, browY + 1, x2 - eyeR/2, browY - 2, ST7735_BLACK);
    } 
    else if (currentEmotion == E_SAD) {
      // Грустные приподнятые брови
      canvas.drawLine(x1 - eyeR/2 + 1, browY - 2, x1 + eyeR/2 - 1, browY + 1, ST7735_BLACK);
      canvas.drawLine(x1 - eyeR/2, browY - 1, x1 + eyeR/2, browY + 2, ST7735_BLACK);
      canvas.drawLine(x2 + eyeR/2 - 1, browY - 2, x2 - eyeR/2 + 1, browY + 1, ST7735_BLACK);
      canvas.drawLine(x2 + eyeR/2, browY - 1, x2 - eyeR/2, browY + 2, ST7735_BLACK);
    }
    else if (currentEmotion == E_SURPRISE) {
      // Удивлённые высоко поднятые брови
      browY -= 5;
      canvas.drawFastHLine(x1 - eyeR/2, browY, eyeR, ST7735_BLACK);
      canvas.drawFastHLine(x1 - eyeR/2, browY+1, eyeR, ST7735_BLACK);
      canvas.drawFastHLine(x2 - eyeR/2, browY, eyeR, ST7735_BLACK);
      canvas.drawFastHLine(x2 - eyeR/2, browY+1, eyeR, ST7735_BLACK);
    }
  }

  // --- ЩЁЧКИ (только для счастливых эмоций) ---
  if (drawCheeks && (!closedLeft || !closedRight)) {
    canvas.drawCircleHelper(x1, baseY + eyeR/2 + 1, eyeR/2, 0x0C, ST7735_BLACK);
    canvas.drawCircleHelper(x2, baseY + eyeR/2 + 1, eyeR/2, 0x0C, ST7735_BLACK);
  }
}

void drawClock() {
  // Градиент для ВСЕХ тем (светлых и тёмных)
  uint16_t c1 = currentTheme->top, c2 = currentTheme->bot;
  for (int y = 0; y < TFT_HEIGHT; y++) {
    uint8_t r = ((c1>>11)&0x1F) + (((c2>>11)&0x1F) - ((c1>>11)&0x1F)) * y / TFT_HEIGHT;
    uint8_t g = ((c1>>5)&0x3F) + (((c2>>5)&0x3F) - ((c1>>5)&0x3F)) * y / TFT_HEIGHT;
    uint8_t b = (c1&0x1F) + ((c2&0x1F) - (c1&0x1F)) * y / TFT_HEIGHT;
    canvas.drawFastHLine(0, y, TFT_WIDTH, (r << 11) | (g << 5) | b);
  }
  
  // Эффекты работают ВСЕГДА
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
    
    // Индикаторы с адаптивными цветами
    if (singleAlarm.active) {
      canvas.fillCircle(10, 10, 3, RGB(255, 80, 80)); // Мягкий красный
    }
    
    if (lampState) {
      canvas.fillCircle(150, 10, 4, RGB(255, 220, 100)); // Янтарный
    }
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
      if (key == IR_BTN_MESSAGE) { currentMode = MODE_QUOTES; quoteIdx = random(NUM_QUOTES); }
      break;
    case MODE_MENU:
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) menuCursor = (menuCursor + 3) % 4;
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) menuCursor = (menuCursor + 1) % 4;
      if (key == IR_BTN_OK) {
        digitBuffer = -1; // Сброс буфера перед входом
        if(menuCursor==0) { currentMode = MODE_SET_TIME; tempRtc = rtc; settingField = 0; }
        if(menuCursor==1) { currentMode = MODE_SET_ALARM; tempAlarm = singleAlarm; settingField = 0; }
        if(menuCursor==2) toggleLamp();
        if(menuCursor==3) { currentMode = MODE_QUOTES; quoteIdx = random(NUM_QUOTES); }
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
      
      // --- ИСПРАВЛЕНИЕ ЗДЕСЬ ---
      if (key == IR_BTN_OK) { 
        rtc = tempRtc; 
        
        // Сбрасываем секунды в 0, чтобы время пошло ровно с новой минуты
        rtc.second = 0; 
        
        // ОБЯЗАТЕЛЬНО: обновляем метку времени на текущий момент
        rtc.lastMillis = millis();
        
        updateThemeByEnv();
        
        currentMode = MODE_CLOCK; 
        showPopup("Сохранено!"); 
      }
      // -------------------------
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
  if (millis() - lastEmotionChange > 15000) {
      // Расширенный пул эмоций с акцентом на позитив и дружелюбие
      EyeEmotion emotionsPool[] = {
          // 💖 ПОЗИТИВНЫЕ (40% — чаще всего!)
          E_HAPPY, E_HAPPY, E_HAPPY, E_HAPPY,    // Счастливый (х4)
          E_IN_LOVE, E_IN_LOVE,                  // Влюблённый (х2)
          E_WINK_LEFT, E_WINK_RIGHT,             // Подмигивания (х2)
          
          // 😐 НЕЙТРАЛЬНЫЕ / ВЗГЛЯД (30% — умеренно)
          E_NEUTRAL, E_NEUTRAL,                  // Нейтральный (х2)
          E_LOOK_LEFT, E_LOOK_RIGHT,             // Взгляд в стороны
          E_LOOK_UP, E_LOOK_DOWN,                // Взгляд вверх/вниз
          
          // 😮 УДИВЛЕНИЕ (10% — для динамики)
          E_SURPRISE, E_SURPRISE,                // Удивлённый (х2)
          
          // 😴 НЕГАТИВ / УСТАЛОСТЬ (20% — редко, но естественно)
          E_TIRED,                               // Уставший (лёгкий прищур)
          E_SLEEPY,                              // Сонный (полный прищур)
          E_ANGRY,                               // Злой (редко!)
          E_SAD                                  // Грустный (редко!)
      };
      
      // Выбираем случайную эмоцию из пула
      currentEmotion = emotionsPool[random(sizeof(emotionsPool) / sizeof(EyeEmotion))];
      
      // Подстраиваем параметры глаз под эмоцию для большей выразительности
      switch (currentEmotion) {
          case E_SURPRISE:
              currentEyeR = random(16, 19);   // Большие глаза от удивления
              currentPupilR = random(2, 3);   // Крошечные зрачки
              currentSpacing = random(65, 72); // Глаза чуть расширяются
              break;
          case E_ANGRY:
              currentEyeR = random(12, 14);   // Узкие "сердитые" глаза
              currentPupilR = random(3, 5);
              currentSpacing = random(58, 65); // Глаза сближаются
              break;
          case E_IN_LOVE:
              currentEyeR = random(14, 16);
              currentPupilR = random(5, 7);   // Большие "влюблённые" зрачки
              currentSpacing = random(62, 68);
              break;
          case E_SLEEPY:
          case E_TIRED:
              currentEyeR = random(13, 15);
              currentPupilR = random(3, 4);   // Маленькие зрачки при усталости
              currentSpacing = random(60, 66);
              break;
          default:
              // Стандартные параметры для остальных эмоций
              currentEyeR = random(13, 16);
              currentPupilR = random(4, 6);
              currentSpacing = random(60, 70);
              break;
      }

      lastEmotionChange = millis();
  }

  // Моргание глаз
  if (!blinkNow && millis() > blinkUntil && random(100) < 3) {
    blinkNow = true;
    blinkUntil = millis() + 120; // 120 мс = прищур
  } else if (blinkNow && millis() > blinkUntil) {
      blinkNow = false;
      blinkUntil = millis() + 10000; // следующее моргание через 3 сек
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
        canvas.setTextColor(ST7735_WHITE); drawCenteredText(50, "ПОДЪЁМ!", FONT_ALARM);
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
