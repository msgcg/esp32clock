#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h"

// --- Таймеры и состояния системы ---
unsigned long lastActivity = 0;          // Таймер бездействия для перехода в "сон"
bool isIdle = false;                     // Состояние анимации (сон)
unsigned long lastKeyTime = 0;           // Антидребезг для кнопок
unsigned long digitInputTime = 0;        // Таймер для двузначного ввода
int digitBuffer = -1;                    // Буфер для цифрового ввода (для двузначных чисел)

// --- Подключение шрифтов с поддержкой кириллицы ---
// Шрифты из FontsRus используют специальную кодировку:
// 0x80-0x8F → р-я (строчные)
// 0x90-0xBF → А-п (заглавные + строчные до 'п')
// 0xC0 → Ё, 0xC1 → ё
#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"
#include "fonts/FontsRus/FreeSans9.h"

// Макросы шрифтов для удобства
#define FONT_HEADER &FreeSansBold9pt8b   // Заголовки меню
#define FONT_TIME   &FreeSansBold14pt8b  // Крупные часы
#define FONT_TEXT   &FreeSans9pt8b       // Основной текст

// --- Константы системы ---
#define TFT_WIDTH  160
#define TFT_HEIGHT 80
#define MAX_ALARMS 3
#define MAX_PARTICLES 15
#define IDLE_TIMEOUT 15000               // 15 сек до перехода в режим сна
#define DIGIT_TIMEOUT 1500               // 1.5 сек для ввода второго разряда

// ---------------- Объекты дисплея и датчиков ----------------
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);
GFXcanvas16 canvas(TFT_WIDTH, TFT_HEIGHT);  // Двойная буферизация для плавности

// ---------------- Цвета и темы (стиль iOS: мягкие градиенты) ----------------
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

struct Theme {
  uint16_t top;      // Верх градиента
  uint16_t bot;      // Низ градиента
  uint16_t text;     // Цвет текста
  uint16_t accent;   // Акцентный цвет (часы, выделение) - НЕ СИНИЙ!
  uint16_t panel;    // Фон панелей и облачков
  uint16_t border;   // Цвет тонких рамок
};

// Темы с мягкими цветами, без резких переходов и без синего для часов
Theme thNight  = { RGB(10,15,30),   RGB(5,8,15),    RGB(220,230,255), RGB(255,220,100), RGB(30,40,60),  RGB(80,100,150) };
Theme thWinter = { RGB(40,60,100),  RGB(20,30,60),  RGB(230,240,255), RGB(150,220,255), RGB(40,50,80),  RGB(120,150,200) };
Theme thSpring = { RGB(30,60,30),   RGB(15,35,15),  RGB(220,255,220), RGB(120,240,120), RGB(35,55,35),  RGB(100,180,100) };
Theme thSummer = { RGB(20,50,100),  RGB(10,30,70),  RGB(240,255,255), RGB(255,200,80),  RGB(25,45,85),  RGB(80,140,200) };
Theme thAutumn = { RGB(60,35,15),   RGB(30,20,10),  RGB(255,240,220), RGB(255,160,60),  RGB(50,35,20),  RGB(150,90,40) };
Theme* currentTheme = &thNight;

// ---------------- Структуры данных ----------------
enum AppMode {
  MODE_CLOCK, MODE_MENU, MODE_SET_TIME, MODE_ALARMS_LIST,
  MODE_ALARM_EDIT, MODE_TUTORIAL, MODE_QUOTES, MODE_RING
};

enum AlarmType { ALARM_OFF, ALARM_ONCE, ALARM_DAILY, ALARM_WEEKDAY };
enum AlarmSound { SOUND_BEEP, SOUND_BIRDS, SOUND_SIREN, SOUND_RAIN };
enum LightMode { LIGHT_OFF, LIGHT_STATIC, LIGHT_BLINK, LIGHT_BEAT };
enum EyeEmotion { EYE_NORMAL, EYE_HAPPY, EYE_SLEEPY, EYE_ANGRY, EYE_CRY, EYE_LAUGH };

struct Alarm {
  uint8_t hour;
  uint8_t minute;
  AlarmType type;
  AlarmSound sound;
  LightMode light;
  bool active;
};

struct SoftClock {
  int year, month, day;
  int hour, minute, second;
  int dayOfWeek;        // 1=Пн, 7=Вс
  unsigned long lastMillis;
};

struct Particle { 
  int x, y; 
  int speed; 
  uint8_t size; 
  bool active; 
  uint16_t color;
};

// ---------------- Глобальные переменные ----------------
AppMode currentMode = MODE_CLOCK;
SoftClock rtc = {2024, 1, 1, 12, 0, 0, 1, 0};
Alarm alarms[MAX_ALARMS];
SoftClock tempRtc;        // Буфер для редактирования времени
Alarm tempAlarm;          // Буфер для редактирования будильника
int currentAlarmIdx = 0;
int menuCursor = 0;
int settingField = 0;     // Текущее выделенное поле в настройках
int tutorialStep = 0;
float temp = 22.0, hum = 45.0;  // Значения по умолчанию до первого чтения
bool lampState = false;
Particle particles[MAX_PARTICLES];
unsigned long lastDraw = 0;
unsigned long lastSensor = 0;
unsigned long popupTimer = 0;
String popupMsg = "";
long nextBlink = 0;
int pupilX = 0, pupilY = 0;
EyeEmotion eyeEmotion = EYE_NORMAL;
int weatherEffect = 0;    // 0=ничего, 1=дождь, 2=снег, 3=ветер
const char* quotes[] = {
  "Улыбнись!", "Ты супер!", "Верь в себя!", "Доброе утро!",
  "Всё получится!", "Не сдавайся!", "Лови момент!", "Ты молодец!",
  "Отдохни немного", "Мечтай смелее!", "Ты справишься!", "Сегодня твой день!"
};
int quoteIdx = 0;
unsigned long soundTimer = 0;
int soundState = 0;
bool isRinging = false;

// --- ФУНКЦИЯ ПЕРЕКОДИРОВКИ ДЛЯ ШРИФТОВ FontsRus ---
// Специальная карта символов для шрифтов из папки FontsRus:
// 0x80-0x8F → р-я (строчные буквы)
// 0x90-0xBF → А-п (заглавные + строчные до 'п')
// 0xC0 → Ё, 0xC1 → ё
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
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2);  // А-Я (0x90-0xBF)
    }
    else if (n == 0xD1 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x91) target += (char)0xC1;  // ё
      else if (n2 >= 0x80 && n2 <= 0x8F) target += (char)(n2);  // а-п (0x80-0x8F)
      else if (n2 >= 0x90 && n2 <= 0xBF) target += (char)(n2 - 0x10);  // р-я (0x90-0xBF → 0x80-0x8F)
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

// --- Показ всплывающего сообщения ---
void showPopup(String msg) {
  popupMsg = msg;
  popupTimer = millis();
}

// --- Переключение состояния лампы (реле) ---
void toggleLamp() {
  lampState = !lampState;
  digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
  showPopup(lampState ? "Свет ВКЛ" : "Свет ВЫКЛ");
}

// --- Инициализация системы ---
void initSystem() {
  // Настройка будильников по умолчанию
  alarms[0] = {7, 0, ALARM_DAILY, SOUND_BIRDS, LIGHT_STATIC, true};
  alarms[1] = {8, 30, ALARM_WEEKDAY, SOUND_RAIN, LIGHT_BLINK, true};
  alarms[2] = {9, 0, ALARM_OFF, SOUND_BEEP, LIGHT_OFF, false};
  
  // Инициализация частиц для погодных эффектов
  for(int i=0; i<MAX_PARTICLES; i++) {
    particles[i].active = false;
  }
  
  // Случайная первая цитата
  quoteIdx = random(0, sizeof(quotes)/sizeof(quotes[0]));
}

// --- Обновление времени через millis() (soft RTC) ---
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
      rtc.day++; 
      rtc.dayOfWeek++;
      if(rtc.dayOfWeek > 7) rtc.dayOfWeek = 1;
      if(rtc.day > 30) { 
        rtc.day = 1; 
        rtc.month++; 
      }
      if(rtc.month > 12) { 
        rtc.month = 1; 
        rtc.year++; 
      }
    }
  }
}

// --- Обновление темы и данных с датчиков ---
void updateThemeAndSensors() {
  if (millis() - lastSensor > 2000) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if(!isnan(t)) temp = t;
    if(!isnan(h)) hum = h;
    
    lastSensor = millis();
    
    // Автоматический выбор темы по времени суток и сезону
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
        weatherEffect = (temp < 0) ? 2 : 1; // Снег при <0°C, иначе дождь
        eyeEmotion = (temp < 0) ? EYE_NORMAL : EYE_HAPPY;
      } else if (rtc.month >= 3 && rtc.month <= 5) {
        currentTheme = &thSpring;
        weatherEffect = (hum > 70) ? 1 : 0; // Дождь при высокой влажности
        eyeEmotion = EYE_HAPPY;
      } else if (rtc.month >= 6 && rtc.month <= 8) {
        currentTheme = &thSummer;
        weatherEffect = (hum > 80) ? 3 : 0; // Ветер/облака при высокой влажности
        eyeEmotion = (temp > 30) ? EYE_ANGRY : EYE_HAPPY;
      } else {
        currentTheme = &thAutumn;
        weatherEffect = (hum > 65) ? 1 : 0; // Дождь осенью
        eyeEmotion = EYE_NORMAL;
      }
    }
    
    // Эмоции глаз в зависимости от погоды
    if (hum > 85 && temp > 15) eyeEmotion = EYE_CRY;    // Сыро = грустные глаза
    if (temp > 32) eyeEmotion = EYE_ANGRY;              // Жарко = злые глаза
    if (rtc.hour >= 12 && rtc.hour <= 14 && temp < 28) eyeEmotion = EYE_LAUGH; // Днём при комфортной температуре
  }
}

// --- Обновление погодных эффектов (дождь, снег) ---
void updateWeatherEffect() {
  if (currentMode != MODE_CLOCK && currentMode != MODE_TUTORIAL && !isIdle) return;
  
  // Инициализация частиц при смене эффекта
  static int lastEffect = -1;
  if (weatherEffect != lastEffect) {
    for(int i=0; i<MAX_PARTICLES; i++) {
      particles[i].active = false;
    }
    lastEffect = weatherEffect;
  }
  
  // Обновление частиц
  for(int i=0; i<MAX_PARTICLES; i++) {
    if (!particles[i].active && random(100) < 5) {
      // Активация новой частицы
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
      } else if (weatherEffect == 3) { // Ветер/листья
        particles[i].speed = random(2, 4);
        particles[i].size = random(1, 2);
        particles[i].color = RGB(180, 120, 40);
      }
    }
    
    if (particles[i].active) {
      particles[i].y += particles[i].speed;
      
      // Отрисовка частицы
      if (weatherEffect == 2) { // Снег - кружочки
        canvas.fillCircle(particles[i].x, particles[i].y, particles[i].size, particles[i].color);
      } else if (weatherEffect == 1) { // Дождь - вертикальные линии
        canvas.drawFastVLine(particles[i].x, particles[i].y, 4, particles[i].color);
      } else if (weatherEffect == 3) { // Ветер - точки
        canvas.drawPixel(particles[i].x, particles[i].y, particles[i].color);
      }
      
      // Удаление частицы за пределами экрана
      if (particles[i].y > TFT_HEIGHT + 10) {
        particles[i].active = false;
      }
    }
  }
}

// --- Проверка срабатывания будильников ---
void checkAlarms() {
  if (rtc.second != 0 || isRinging) return; // Проверяем только на начало минуты
  
  for(int i=0; i<MAX_ALARMS; i++) {
    if (alarms[i].type == ALARM_OFF || !alarms[i].active) continue;
    
    bool trigger = false;
    if (alarms[i].hour == rtc.hour && alarms[i].minute == rtc.minute) {
      if (alarms[i].type == ALARM_ONCE) trigger = true;
      if (alarms[i].type == ALARM_DAILY) trigger = true;
      if (alarms[i].type == ALARM_WEEKDAY && rtc.dayOfWeek <= 5) trigger = true;
    }
    
    if (trigger) {
      currentMode = MODE_RING;
      currentAlarmIdx = i;
      isRinging = true;
      
      // Включение света согласно настройкам будильника
      if (alarms[i].light != LIGHT_OFF) {
        lampState = true;
        digitalWrite(PIN_RELAY, HIGH);
      }
      
      // Однократный будильник деактивируется после срабатывания
      if (alarms[i].type == ALARM_ONCE) {
        alarms[i].active = false;
      }
      
      break; // Срабатывает только первый активный будильник
    }
  }
}

// --- Проигрывание звука будильника с синхронизацией света ---
void playAlarmSound() {
  if (!isRinging) return;
  
  AlarmSound soundMode = alarms[currentAlarmIdx].sound;
  LightMode lightMode = alarms[currentAlarmIdx].light;
  unsigned long now = millis();
  unsigned long interval = 600; // Базовый интервал
  
  switch(soundMode) {
    case SOUND_BEEP: interval = 800; break;
    case SOUND_BIRDS: interval = 200; break;
    case SOUND_SIREN: interval = 300; break;
    case SOUND_RAIN: interval = 150; break;
  }
  
  if (now - soundTimer > interval) {
    soundTimer = now;
    soundState++;
    
    // Остановка предыдущего тона
    noTone(PIN_BUZZER);
    
    // Логика звуков
    switch(soundMode) {
      case SOUND_BEEP:
        if (soundState % 2 == 0) {
          tone(PIN_BUZZER, 1000, 300);
          if (lightMode == LIGHT_BEAT) digitalWrite(PIN_RELAY, HIGH);
        } else {
          if (lightMode == LIGHT_BEAT) digitalWrite(PIN_RELAY, LOW);
        }
        break;
        
      case SOUND_BIRDS:
        if (soundState % 4 == 0) tone(PIN_BUZZER, 2500, 80);
        else if (soundState % 4 == 1) tone(PIN_BUZZER, 3200, 70);
        else if (soundState % 4 == 2) tone(PIN_BUZZER, 2800, 90);
        else if (soundState % 4 == 3) tone(PIN_BUZZER, 3500, 60);
        
        if (lightMode == LIGHT_BEAT && (soundState % 4 == 0 || soundState % 4 == 2)) {
          digitalWrite(PIN_RELAY, HIGH);
          delay(50);
          digitalWrite(PIN_RELAY, LOW);
        }
        break;
        
      case SOUND_SIREN:
        if (soundState % 2 == 0) tone(PIN_BUZZER, 700, 250);
        else tone(PIN_BUZZER, 1400, 250);
        
        if (lightMode == LIGHT_BEAT) {
          digitalWrite(PIN_RELAY, (soundState % 2 == 0) ? HIGH : LOW);
        }
        break;
        
      case SOUND_RAIN:
        if (soundState % 3 == 0) tone(PIN_BUZZER, 600, 50);
        else if (soundState % 3 == 1) tone(PIN_BUZZER, 800, 40);
        else tone(PIN_BUZZER, 700, 45);
        
        if (lightMode == LIGHT_BEAT && soundState % 3 == 0) {
          digitalWrite(PIN_RELAY, HIGH);
          delay(30);
          digitalWrite(PIN_RELAY, LOW);
        }
        break;
    }
    
    // Логика света для режимов кроме BEAT
    if (lightMode == LIGHT_BLINK) {
      digitalWrite(PIN_RELAY, (soundState % 2 == 0) ? HIGH : LOW);
    } else if (lightMode == LIGHT_STATIC) {
      digitalWrite(PIN_RELAY, HIGH);
    }
  }
}

// --- Отрисовка градиентного фона ---
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

// --- Отрисовка глаз с эмоциями (стиль милых персонажей) ---
void drawEyes(int x_offset) {
  int x1 = 45 + x_offset, y1 = 42;
  int x2 = 115 + x_offset, y2 = 42;
  int r = 14;
  
  // Мигание глаз (каждые 3-6 секунд)
  if (millis() > nextBlink && millis() < nextBlink + 150) {
    // Глаза закрыты
    canvas.drawLine(x1-15, y1, x1+15, y1, ST7735_WHITE);
    canvas.drawLine(x2-15, y2, x2+15, y2, ST7735_WHITE);
    return;
  } else if (millis() > nextBlink + 150) {
    nextBlink = millis() + random(3000, 6000);
  }
  
  // Форма глаз (всегда круглые)
  canvas.fillCircle(x1, y1, r, ST7735_WHITE);
  canvas.fillCircle(x2, y2, r, ST7735_WHITE);
  
  // Зрачки с плавным движением
  static unsigned long lastPupilMove = 0;
  if (millis() - lastPupilMove > 2000) {
    pupilX = random(-4, 5);
    pupilY = random(-3, 4);
    lastPupilMove = millis();
  }
  
  // Основные зрачки
  canvas.fillCircle(x1 + pupilX, y1 + pupilY, 5, ST7735_BLACK);
  canvas.fillCircle(x2 + pupilX, y2 + pupilY, 5, ST7735_BLACK);
  
  // Добавление эмоций (без drawArc - используем линии и прямоугольники)
  switch(eyeEmotion) {
    case EYE_HAPPY: // Счастливые глаза (улыбающиеся дуги через линии)
      canvas.drawLine(x1-8, y1+8, x1, y1+12, ST7735_BLACK);
      canvas.drawLine(x1, y1+12, x1+8, y1+8, ST7735_BLACK);
      canvas.drawLine(x2-8, y2+8, x2, y2+12, ST7735_BLACK);
      canvas.drawLine(x2, y2+12, x2+8, y2+8, ST7735_BLACK);
      break;
      
    case EYE_SLEEPY: // Сонные глаза (полуприкрытые)
      canvas.fillRect(x1-15, y1-3, 30, 6, currentTheme->top);
      canvas.fillRect(x2-15, y2-3, 30, 6, currentTheme->top);
      break;
      
    case EYE_ANGRY: // Злые глазы (брови через линии)
      canvas.drawLine(x1-12, y1-12, x1-3, y1-6, ST7735_BLACK);
      canvas.drawLine(x1+3, y1-6, x1+12, y1-12, ST7735_BLACK);
      canvas.drawLine(x2-12, y2-12, x2-3, y2-6, ST7735_BLACK);
      canvas.drawLine(x2+3, y2-6, x2+12, y2-12, ST7735_BLACK);
      break;
      
    case EYE_CRY: // Плачущие глаза (слезы)
      canvas.fillCircle(x1+8, y1+12, 2, ST7735_BLUE);
      canvas.fillCircle(x2-8, y2+12, 2, ST7735_BLUE);
      canvas.drawLine(x1+8, y1+14, x1+8, y1+18, ST7735_BLUE);
      canvas.drawLine(x2-8, y2+14, x2-8, y2+18, ST7735_BLUE);
      break;
      
    case EYE_LAUGH: // Смеющиеся глаза (закрыты от смеха)
      canvas.drawLine(x1-10, y1, x1+10, y1, ST7735_BLACK);
      canvas.drawLine(x2-10, y2, x2+10, y2, ST7735_BLACK);
      // Щёчки
      canvas.fillCircle(x1-12, y1+8, 4, RGB(255, 150, 150));
      canvas.fillCircle(x2+12, y2+8, 4, RGB(255, 150, 150));
      break;
      
    default: // Нормальные глаза - ничего не добавляем
      break;
  }
}

// --- Отрисовка элемента списка с эффектом скругления ---
void drawListItem(int y, String text, bool selected) {
  if (selected) {
    // Стиль выделения: мягкий фон панели
    canvas.fillRoundRect(8, y+2, TFT_WIDTH-16, 18, 5, currentTheme->panel);
    canvas.drawRoundRect(8, y+2, TFT_WIDTH-16, 18, 5, currentTheme->border);
    canvas.setTextColor(currentTheme->accent);
  } else {
    canvas.setTextColor(currentTheme->text);
  }
  
  canvas.setCursor(20, y + 15);
  canvas.print(utf8rus(text));
}

// --- Основной экран часов ---
void drawScreenClock() {
  drawGradient();
  
  if (isIdle) {
    // Режим сна: глаза и плавающая цитата
    drawEyes(0);
    
    // Облачко с цитатой (появляется каждые 8 секунд)
    if ((millis() / 8000) % 2 == 0) {
      canvas.fillRoundRect(15, 10, TFT_WIDTH-30, 30, 8, currentTheme->panel);
      canvas.drawRoundRect(15, 10, TFT_WIDTH-30, 30, 8, currentTheme->border);
      canvas.setFont(FONT_TEXT);
      canvas.setTextColor(currentTheme->accent);
      canvas.setCursor(25, 28);
      canvas.print(utf8rus(quotes[quoteIdx]));
    }
  } else {
    // Крупные часы по центру с мягким акцентным цветом (не синий!)
    canvas.setFont(FONT_TIME);
    canvas.setTextSize(1);
    
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", rtc.hour, rtc.minute);
    
    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    
    // Мягкий акцентный цвет (желто-золотой вместо режущего синего)
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor((TFT_WIDTH - w) / 2, 48);
    canvas.print(timeStr);
    
    // Дата и погода внизу экрана
    canvas.setFont(FONT_TEXT);
    canvas.setTextColor(currentTheme->text);
    
    String dateStr = String(rtc.day) + "." + String(rtc.month) + "." + String(rtc.year % 100);
    String weatherStr = String((int)temp) + "C " + String((int)hum) + "%";
    
    canvas.getTextBounds(utf8rus(dateStr), 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor(5, TFT_HEIGHT - 5);
    canvas.print(utf8rus(dateStr));
    
    canvas.getTextBounds(utf8rus(weatherStr), 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor(TFT_WIDTH - w - 5, TFT_HEIGHT - 5);
    canvas.print(utf8rus(weatherStr));
  }
  
  // Погодные эффекты поверх всего
  updateWeatherEffect();
}

// --- Главное меню ---
void drawMenu() {
  drawGradient();
  
  // Заголовок меню
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 40) / 2, 18);
  canvas.print(utf8rus("МЕНЮ"));
  
  // Элементы меню
  canvas.setFont(FONT_TEXT);
  const char* items[] = {"Будильники", "Время и дата", "Управление светом", "Обучение", "Цитаты"};
  int totalItems = 5;
  
  // Прокрутка меню (показываем 3 элемента)
  int start = (menuCursor > 1) ? menuCursor - 1 : 0;
  if (start > totalItems - 3) start = totalItems - 3;
  
  int y = 30;
  for(int i = 0; i < 3 && start + i < totalItems; i++) {
    int idx = start + i;
    drawListItem(y, items[idx], idx == menuCursor);
    y += 22;
  }
}

// --- Экран установки времени и даты ---
void drawSetTime() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor((TFT_WIDTH - 100) / 2, 15);
  canvas.print(utf8rus("ВРЕМЯ И ДАТА"));
  
  canvas.setFont(FONT_TEXT);
  
  // Поля ввода с подсветкой активного поля
  const char* labels[] = {"Часы", "Минуты", "День", "Месяц"};
  int values[] = {tempRtc.hour, tempRtc.minute, tempRtc.day, tempRtc.month};
  
  int y = 35;
  for(int i=0; i<4; i++) {
    // Метка поля
    canvas.setTextColor(currentTheme->text);
    canvas.setCursor(10, y + 12);
    canvas.print(utf8rus(labels[i]));
    canvas.print(": ");
    
    // Значение поля
    canvas.setTextColor((i == settingField) ? currentTheme->accent : currentTheme->text);
    if (values[i] < 10) canvas.print("0");
    canvas.print(values[i]);
    
    y += 16;
  }
  
  // Подсказка управления
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(15, 75);
  canvas.print(utf8rus("Стрелки - выбор, +/- - изменить"));
}

// --- Экран редактирования будильника ---
void drawAlarmEdit() {
  drawGradient();
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor(10, 15);
  canvas.print(utf8rus("Будильник ") + String(currentAlarmIdx+1));
  
  canvas.setFont(FONT_TEXT);
  
  // Подготовка данных для отображения
  String items[5];
  items[0] = "Время: " + String(tempAlarm.hour < 10 ? "0" : "") + String(tempAlarm.hour) + 
             ":" + String(tempAlarm.minute < 10 ? "0" : "") + String(tempAlarm.minute);
  
  const char* types[] = {"Выключен", "Один раз", "Ежедневно", "Будни"};
  items[1] = "Тип: " + String(types[tempAlarm.type]);
  
  const char* sounds[] = {"Бип", "Птицы", "Сирена", "Дождь"};
  items[2] = "Звук: " + String(sounds[tempAlarm.sound]);
  
  const char* lights[] = {"Без света", "Статика", "Мигание", "В такт звуку"};
  items[3] = "Свет: " + String(lights[tempAlarm.light]);
  
  items[4] = "[ Готово ]";
  
  // Отображение списка с прокруткой
  int start = (settingField > 2) ? settingField - 2 : 0;
  for(int i=0; i<3 && start+i<5; i++) {
    int idx = start + i;
    drawListItem(25 + i*19, items[idx], idx == settingField);
  }
  
  // Подсказка управления
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(10, 75);
  canvas.print(utf8rus("200+ - удалить будильник"));
}

// --- Экран обучения (расширенный до 8 шагов) ---
void drawTutorial() {
  canvas.fillScreen(RGB(20, 25, 40));
  
  // Фон в стиле мягкий градиент
  for(int y=0; y<TFT_HEIGHT; y++) {
    uint8_t shade = 20 + (y * 15 / TFT_HEIGHT);
    canvas.drawFastHLine(0, y, TFT_WIDTH, RGB(shade, shade+5, shade+15));
  }
  
  canvas.setFont(FONT_HEADER);
  canvas.setTextColor(RGB(255, 220, 100));
  canvas.setCursor(45, 15);
  canvas.print(utf8rus("ОБУЧЕНИЕ"));
  
  canvas.setFont(FONT_TEXT);
  canvas.setTextColor(currentTheme->text);
  
  String txt = "";
  switch(tutorialStep) {
    case 0: txt = "Привет!\nЭто умные часы с будильником и погодой."; break;
    case 1: txt = "Управление:\nEQ - главное меню, CH - домой"; break;
    case 2: txt = "Кнопка CH- включает и выключает свет (реле). Удобно ночью!"; break;
    case 3: txt = "Кнопка CH+ показывает цитаты и советы. Листайте стрелками."; break;
    case 4: txt = "Будильники:\nНастройте звук, свет и расписание. 100+ = сохранить"; break;
    case 5: txt = "Часы показывают температуру и влажность. Глазки меняют настроение!"; break;
    case 6: txt = "Через 15 секунд бездействия часы засыпают и показывают цитаты."; break;
    case 7: txt = "Готово!\nНаслаждайтесь часами! Нажмите EQ чтобы выйти."; break;
  }
  
  // Разбивка текста на строки
  int y = 30;
  int startPos = 0;
  for (int i=0; i<=txt.length(); i++) {
    if (i == txt.length() || txt[i] == '\n') {
      canvas.setCursor(10, y);
      canvas.print(utf8rus(txt.substring(startPos, i)));
      y += 16;
      startPos = i + 1;
    }
  }
  
  // Индикатор прогресса
  canvas.setTextColor(RGB(150, 180, 220));
  canvas.setCursor(5, 75);
  canvas.print(String(tutorialStep+1) + "/8");
}

// --- Экран срабатывания будильника ---
void drawRing() {
  // Пульсирующий фон для привлечения внимания
  uint16_t bgColor = ((millis() / 400) % 2 == 0) ? RGB(40, 20, 20) : RGB(60, 30, 30);
  canvas.fillScreen(bgColor);
  
  // Крупная надпись "ПОДЪЕМ!"
  canvas.setFont(FONT_TIME);
  canvas.setTextColor(ST7735_WHITE);
  canvas.setCursor(20, 45);
  canvas.print(utf8rus("ПОДЪЕМ!"));
  
  // Индикация режима света
  if (alarms[currentAlarmIdx].light != LIGHT_OFF) {
    canvas.drawCircle(145, 20, 8, ST7735_YELLOW);
    canvas.fillCircle(145, 20, 6, (millis()/300)%2 ? ST7735_YELLOW : ST7735_BLACK);
  }
}

// --- Экран цитат ---
void drawQuotesScreen() {
  drawGradient();
  
  // Облачко в центре экрана
  canvas.fillRoundRect(15, 20, TFT_WIDTH-30, 40, 10, currentTheme->panel);
  canvas.drawRoundRect(15, 20, TFT_WIDTH-30, 40, 10, currentTheme->border);
  
  canvas.setFont(FONT_TEXT);
  canvas.setTextColor(currentTheme->accent);
  canvas.setCursor(25, 43);
  canvas.print(utf8rus(quotes[quoteIdx]));
  
  // Подсказка управления
  canvas.setTextColor(RGB(180, 180, 200));
  canvas.setCursor(25, 75);
  canvas.print(utf8rus("CH - назад, Стрелки - следующая"));
}

// --- Обработка IR-сигналов с поддержкой двузначного ввода ---
void handleIR() {
  if (!irrecv.decode(&results)) return;
  
  unsigned long key = results.value;
  unsigned long now = millis();
  
  // Антидребезг: игнорируем повторы чаще 150мс
  if (now - lastKeyTime < 150) {
    irrecv.resume();
    return;
  }
  lastKeyTime = now;
  
  // Получаем цифру, если нажата цифровая клавиша
  int digit = getDigitFromKey(key);
  
  // ЛЮБАЯ кнопка пробуждает часы из режима сна
  if (isIdle) {
    isIdle = false;
    lastActivity = millis();
    
    // При пробуждении меняем цитату
    if (key != IR_BTN_CH) {
      quoteIdx = (quoteIdx + 1) % (sizeof(quotes)/sizeof(quotes[0]));
    }
    
    irrecv.resume();
    return;
  }
  
  // Сброс таймера активности
  lastActivity = millis();
  
  // === ГЛОБАЛЬНАЯ ОБРАБОТКА КНОПКИ CH- (УПРАВЛЕНИЕ РЕЛЕ) ===
  // Работает на ЛЮБОМ экране кроме режима звонка будильника
  if (currentMode != MODE_RING && key == IR_BTN_CH_MINUS) {
    toggleLamp();
    irrecv.resume();
    return;
  }
  
  // Сброс будильника ЛЮБОЙ кнопкой (кроме CH- чтобы не выключить свет случайно)
  if (currentMode == MODE_RING) {
    if (key != IR_BTN_CH_MINUS) {
      isRinging = false;
      noTone(PIN_BUZZER);
      if (alarms[currentAlarmIdx].light != LIGHT_OFF) {
        digitalWrite(PIN_RELAY, LOW);
        lampState = false;
      }
      currentMode = MODE_CLOCK;
      irrecv.resume();
      return;
    }
  }
  
  // Обработка режима обучения
  if (currentMode == MODE_TUTORIAL) {
    if (key == IR_BTN_NEXT || key == IR_BTN_PLUS || key == IR_BTN_PLAY) {
      tutorialStep++;
      if (tutorialStep > 7) tutorialStep = 7;
    }
    if (key == IR_BTN_PREV || key == IR_BTN_MINUS) {
      tutorialStep--;
      if (tutorialStep < 0) tutorialStep = 0;
    }
    if (key == IR_BTN_EQ || key == IR_BTN_100PLUS) {
      currentMode = MODE_CLOCK;
      showPopup("Обучение завершено");
    }
    irrecv.resume();
    return;
  }
  
  // Обработка основных режимов
  switch (currentMode) {
    case MODE_CLOCK:
      if (key == IR_BTN_CH) { 
        currentMode = MODE_CLOCK; 
        settingField = 0;
        menuCursor = 0; 
      }
      if (key == IR_BTN_EQ) { 
        currentMode = MODE_MENU; 
        menuCursor = 0; 
      }
      if (key == IR_BTN_CH_PLUS) { 
        currentMode = MODE_QUOTES; 
        quoteIdx = random(0, sizeof(quotes)/sizeof(quotes[0])); 
      }
      if (key == IR_BTN_200PLUS) {
        for(int i=0; i<MAX_ALARMS; i++) {
          alarms[i].active = false;
          alarms[i].type = ALARM_OFF;
        }
        showPopup("Все будильники отключены");
      }
      break;
      
    case MODE_MENU:
      if (key == IR_BTN_EQ || key == IR_BTN_CH) { 
        currentMode = MODE_CLOCK; 
      }
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { 
        menuCursor--; 
        if (menuCursor < 0) menuCursor = 4; 
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { 
        menuCursor++; 
        if (menuCursor > 4) menuCursor = 0; 
      }
      if (key == IR_BTN_100PLUS || key == IR_BTN_PLAY) {
        switch(menuCursor) {
          case 0: // Будильники
            currentMode = MODE_ALARMS_LIST; 
            menuCursor = 0; 
            break;
          case 1: // Время/Дата
            currentMode = MODE_SET_TIME;
            settingField = 0;
            tempRtc = rtc; // Копируем текущее время во временный буфер
            break;
          case 2: // Свет - уже обработано глобально через CH-
            break;
          case 3: // Обучение
            currentMode = MODE_TUTORIAL;
            tutorialStep = 0;
            break;
          case 4: // Цитаты
            currentMode = MODE_QUOTES;
            quoteIdx = random(0, sizeof(quotes)/sizeof(quotes[0]));
            break;
        }
      }
      break;
      
    case MODE_SET_TIME:
      if (key == IR_BTN_EQ || key == IR_BTN_CH) { 
        currentMode = MODE_CLOCK; 
        showPopup("Изменения отменены");
      }
      if (key == IR_BTN_100PLUS) {
        rtc = tempRtc; // Применяем изменения
        currentMode = MODE_CLOCK;
        showPopup("Время установлено");
      }
      // Навигация по полям
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { 
        settingField = (settingField - 1 + 4) % 4; 
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { 
        settingField = (settingField + 1) % 4; 
      }
      // Изменение значений
      if (key == IR_BTN_PLUS) {
        switch(settingField) {
          case 0: tempRtc.hour = (tempRtc.hour + 1) % 24; break;
          case 1: tempRtc.minute = (tempRtc.minute + 1) % 60; break;
          case 2: tempRtc.day = (tempRtc.day % 31) + 1; break;
          case 3: tempRtc.month = (tempRtc.month % 12) + 1; break;
        }
      }
      if (key == IR_BTN_MINUS) {
        switch(settingField) {
          case 0: tempRtc.hour = (tempRtc.hour + 23) % 24; break;
          case 1: tempRtc.minute = (tempRtc.minute + 59) % 60; break;
          case 2: tempRtc.day = (tempRtc.day + 29) % 31 + 1; break;
          case 3: tempRtc.month = (tempRtc.month + 10) % 12 + 1; break;
        }
      }
      // === ЦИФРОВОЙ ВВОД С ДВУЗНАЧНОЙ ПОДДЕРЖКОЙ ===
      if (digit != -1) {
        if (now - digitInputTime < DIGIT_TIMEOUT && digitBuffer != -1) {
          // Вторая цифра - формируем двузначное число
          int value = digitBuffer * 10 + digit;
          
          switch(settingField) {
            case 0: if (value < 24) tempRtc.hour = value; break;
            case 1: if (value < 60) tempRtc.minute = value; break;
            case 2: if (value > 0 && value <= 31) tempRtc.day = value; break;
            case 3: if (value > 0 && value <= 12) tempRtc.month = value; break;
          }
          
          digitBuffer = -1; // Сбрасываем буфер
        } else {
          // Первая цифра - сохраняем в буфер
          digitBuffer = digit;
          digitInputTime = now;
        }
      }
      break;
      
    case MODE_ALARMS_LIST:
      if (key == IR_BTN_EQ || key == IR_BTN_CH) { 
        currentMode = MODE_MENU; 
      }
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { 
        menuCursor--; 
        if(menuCursor < 0) menuCursor = MAX_ALARMS-1; 
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { 
        menuCursor++; 
        if(menuCursor >= MAX_ALARMS) menuCursor = 0; 
      }
      if (key == IR_BTN_100PLUS || key == IR_BTN_PLAY) {
        currentAlarmIdx = menuCursor;
        tempAlarm = alarms[currentAlarmIdx]; // Копируем в буфер редактирования
        currentMode = MODE_ALARM_EDIT;
        settingField = 0;
      }
      if (key == IR_BTN_200PLUS) {
        // Быстрое отключение будильника из списка
        alarms[menuCursor].active = false;
        alarms[menuCursor].type = ALARM_OFF;
        showPopup("Будильник " + String(menuCursor+1) + " отключен");
      }
      break;
      
    case MODE_ALARM_EDIT:
      if (key == IR_BTN_EQ || key == IR_BTN_CH) { 
        currentMode = MODE_ALARMS_LIST; 
        showPopup("Изменения отменены");
      }
      if (key == IR_BTN_100PLUS || key == IR_BTN_PLAY) {
        // Сохранение будильника
        alarms[currentAlarmIdx] = tempAlarm;
        alarms[currentAlarmIdx].active = (tempAlarm.type != ALARM_OFF);
        currentMode = MODE_ALARMS_LIST;
        showPopup("Будильник сохранен");
      }
      // Навигация по полям (0-3: настройки, 4: кнопка сохранения)
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { 
        settingField = (settingField - 1 + 5) % 5; 
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { 
        settingField = (settingField + 1) % 5; 
      }
      // Изменение значений для полей 0-3
      if (settingField < 4) {
        if (key == IR_BTN_PLUS) {
          switch(settingField) {
            case 0: // Время - сложная логика для часов и минут
              tempAlarm.minute++;
              if (tempAlarm.minute >= 60) {
                tempAlarm.minute = 0;
                tempAlarm.hour = (tempAlarm.hour + 1) % 24;
              }
              break;
            case 1: // Тип будильника
              tempAlarm.type = (AlarmType)(((int)tempAlarm.type + 1) % 4);
              break;
            case 2: // Звук
              tempAlarm.sound = (AlarmSound)(((int)tempAlarm.sound + 1) % 4);
              break;
            case 3: // Режим света
              tempAlarm.light = (LightMode)(((int)tempAlarm.light + 1) % 4);
              break;
          }
        }
        if (key == IR_BTN_MINUS) {
          switch(settingField) {
            case 0:
              if (tempAlarm.minute == 0) {
                tempAlarm.minute = 59;
                tempAlarm.hour = (tempAlarm.hour + 23) % 24;
              } else {
                tempAlarm.minute--;
              }
              break;
            case 1: 
              tempAlarm.type = (AlarmType)(((int)tempAlarm.type + 3) % 4); 
              break;
            case 2: 
              tempAlarm.sound = (AlarmSound)(((int)tempAlarm.sound + 3) % 4); 
              break;
            case 3: 
              tempAlarm.light = (LightMode)(((int)tempAlarm.light + 3) % 4); 
              break;
          }
        }
        // Цифровой ввод времени
        if (digit != -1 && settingField == 0) {
          if (now - digitInputTime < DIGIT_TIMEOUT && digitBuffer != -1) {
            int minutes = digitBuffer * 10 + digit;
            if (minutes < 60) tempAlarm.minute = minutes;
            digitBuffer = -1;
          } else {
            digitBuffer = digit;
            digitInputTime = now;
          }
        }
      }
      // Удаление будильника
      if (key == IR_BTN_200PLUS && settingField == 4) {
        alarms[currentAlarmIdx].type = ALARM_OFF;
        alarms[currentAlarmIdx].active = false;
        currentMode = MODE_ALARMS_LIST;
        showPopup("Будильник удален");
      }
      break;
      
    case MODE_QUOTES:
      if (key == IR_BTN_CH || key == IR_BTN_EQ) {
        currentMode = MODE_CLOCK;
      }
      if (key == IR_BTN_NEXT || key == IR_BTN_PLUS || key == IR_BTN_PLAY) {
        quoteIdx = (quoteIdx + 1) % (sizeof(quotes)/sizeof(quotes[0]));
      }
      if (key == IR_BTN_PREV || key == IR_BTN_MINUS) {
        quoteIdx = (quoteIdx + sizeof(quotes)/sizeof(quotes[0]) - 1) % (sizeof(quotes)/sizeof(quotes[0]));
      }
      break;
  }
  
  irrecv.resume();
}

// ---------------- SETUP и LOOP ----------------
void setup() {
  // Инициализация пинов
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  
  // Инициализация датчиков
  dht.begin();
  irrecv.enableIRIn();
  
  // Инициализация дисплея
  tft.initR(INITR_MINI160x80);
  tft.setRotation(3);
  tft.fillScreen(ST7735_BLACK);
  
  // Инициализация системы
  initSystem();
  updateThemeAndSensors();
  
  // Начинаем с обучения при первом запуске
  currentMode = MODE_TUTORIAL;
  tutorialStep = 0;
  
  // Инициализация времени
  rtc.lastMillis = millis();
  lastActivity = millis();
  lastDraw = millis();
  
  // Инициализация случайных чисел
  randomSeed(analogRead(0));
}

void loop() {
  // Обновление системного времени
  updateTime();
  
  // Обновление темы и данных с датчиков
  updateThemeAndSensors();
  
  // Проверка будильников
  checkAlarms();
  
  // Обработка нажатий кнопок
  handleIR();
  
  // Автоматический переход в режим сна при бездействии
  if (currentMode == MODE_CLOCK && !isRinging && millis() - lastActivity > IDLE_TIMEOUT) {
    isIdle = true;
  }
  
  // Проигрывание звука будильника
  if (currentMode == MODE_RING && isRinging) {
    playAlarmSound();
  }
  
  // Отрисовка экрана с частотой ~20 кадров/сек (каждые 50мс)
  if (millis() - lastDraw > 50) {
    // Очистка буфера перед отрисовкой
    canvas.fillScreen(ST7735_BLACK);
    
    // Отрисовка текущего режима
    switch (currentMode) {
      case MODE_CLOCK: 
        drawScreenClock(); 
        break;
        
      case MODE_MENU: 
        drawMenu(); 
        break;
        
      case MODE_SET_TIME: 
        drawSetTime(); 
        break;
        
      case MODE_ALARMS_LIST:
        drawGradient();
        canvas.setFont(FONT_TEXT);
        for(int i=0; i<MAX_ALARMS; i++) {
          const char* types[] = {"Выкл", "Один", "Ежедн", "Будни"};
          String status = (alarms[i].active) ? types[alarms[i].type] : "Спящий";
          String timeStr = String(alarms[i].hour) + ":" + (alarms[i].minute < 10 ? "0" : "") + String(alarms[i].minute);
          String s = "Буд." + String(i+1) + " " + status + " " + timeStr;
          drawListItem(8 + i*24, s, i == menuCursor);
        }
        // Подсказка
        canvas.setTextColor(RGB(180, 180, 200));
        canvas.setCursor(10, 75);
        canvas.print(utf8rus("100+ Настроить, 200+ Выкл"));
        break;
        
      case MODE_ALARM_EDIT: 
        drawAlarmEdit(); 
        break;
        
      case MODE_TUTORIAL: 
        drawTutorial(); 
        break;
        
      case MODE_QUOTES: 
        drawQuotesScreen(); 
        break;
        
      case MODE_RING: 
        drawRing(); 
        break;
    }
    
    // Отображение всплывающего сообщения (если есть)
    if (millis() - popupTimer < 2000 && popupMsg != "") {
      canvas.fillRoundRect(20, 30, TFT_WIDTH-40, 22, 6, RGB(60, 80, 120));
      canvas.drawRoundRect(20, 30, TFT_WIDTH-40, 22, 6, currentTheme->border);
      canvas.setTextColor(ST7735_WHITE);
      canvas.setFont(FONT_TEXT);
      
      // Центрирование текста
      int16_t x1, y1;
      uint16_t w, h;
      canvas.getTextBounds(utf8rus(popupMsg), 0, 0, &x1, &y1, &w, &h);
      canvas.setCursor((TFT_WIDTH - w) / 2, 45);
      canvas.print(utf8rus(popupMsg));
    }
    
    // Вывод буфера на экран (двойная буферизация)
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), TFT_WIDTH, TFT_HEIGHT);
    
    lastDraw = millis();
  }
}