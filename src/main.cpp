#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h" 

// --- Подключение шрифтов ---
#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"
#include "fonts/FontsRus/FreeSans9.h" // Обычный шрифт для меню и попапов

// Макросы шрифтов
#define FONT_HEADER &FreeSansBold9pt8b // Заголовки
#define FONT_TIME   &FreeSansBold14pt8b // Часы
#define FONT_TEXT   &FreeSans9pt8b      // Обычный текст (меню, попапы)

// --- Настройки ---
#define TFT_WIDTH  160
#define TFT_HEIGHT 80
#define MAX_ALARMS 3      
#define MAX_PARTICLES 10  

// ---------------- Объекты ----------------
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);
GFXcanvas16 canvas(TFT_WIDTH, TFT_HEIGHT);

// ---------------- Цвета и Темы ----------------
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

struct Theme {
    uint16_t top;    
    uint16_t bot;    
    uint16_t text;   
    uint16_t accent; 
    uint16_t panel;  
};

Theme thNight  = { RGB(10,10,30),   RGB(0,0,0),       RGB(200,200,200), RGB(255,200,0),   RGB(40,40,50) };
Theme thWinter = { RGB(60,80,140),  RGB(200,220,255), RGB(20,20,20),    RGB(0,0,255),     RGB(200,200,220) }; // Текст темный на светлом фоне
Theme thSpring = { RGB(40,120,60),  RGB(180,255,100), RGB(255,255,240), RGB(255,100,150), RGB(20,80,40) };
Theme thSummer = { RGB(0,150,250),  RGB(255,230,100), RGB(255,255,255), RGB(255,165,0),   RGB(0,100,200) };
Theme thAutumn = { RGB(140,70,0),   RGB(255,180,0),   RGB(255,240,200), RGB(255,215,0),   RGB(100,50,0) };

Theme* currentTheme = &thNight;

// ---------------- Структуры данных ----------------

enum AppMode {
    MODE_CLOCK, MODE_MENU, MODE_SET_TIME, MODE_ALARMS_LIST, 
    MODE_ALARM_EDIT, MODE_TUTORIAL, MODE_QUOTES, MODE_RING
};

enum AlarmType { ALARM_OFF, ALARM_ONCE, ALARM_DAILY, ALARM_WEEKDAY };
enum AlarmSound { SOUND_BEEP, SOUND_BIRDS, SOUND_SIREN };
enum LightMode { LIGHT_STATIC, LIGHT_BLINK, LIGHT_OFF };

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
    int dayOfWeek;
    unsigned long lastMillis;
};

struct Particle { int x, y, speed; bool active; };

// ---------------- Глобальные переменные ----------------
AppMode currentMode = MODE_CLOCK;
SoftClock rtc = {2024, 1, 1, 12, 0, 0, 1, 0};
Alarm alarms[MAX_ALARMS];

// -- Временные переменные для редактирования (буфер) --
SoftClock tempRtc;     // Черновик времени
Alarm tempAlarm;       // Черновик будильника
int currentAlarmIdx = 0;

int menuCursor = 0;
int settingField = 0; 
int tutorialStep = 0;

float temp = 0, hum = 0;
bool lampState = false;
Particle particles[MAX_PARTICLES];

unsigned long lastDraw = 0;
unsigned long lastSensor = 0;
unsigned long popupTimer = 0;
String popupMsg = "";

long nextBlink = 0;
int pupilX = 0, pupilY = 0;
int eyeState = 0; 

const char* quotes[] = {
    "Улыбнись!", "Ты супер!", "Верь в себя!", "Доброе утро!", 
    "Всё получится!", "Не сдавайся!", "Лови момент!"
};
int quoteIdx = 0;

unsigned long soundTimer = 0;
int soundState = 0;

// --- Функция перекодировки (для FontsRus) ---
String utf8rus(String source) {
  String target = "";
  int len = source.length();
  for (int i = 0; i < len; i++) {
    unsigned char n = source[i];
    if (n < 127) { target += (char)n; }
    else if (n == 0xD0 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x81) target += (char)0xC0; 
      else target += (char)n2; 
    }
    else if (n == 0xD1 && i+1 < len) {
      unsigned char n2 = source[++i];
      if (n2 == 0x91) target += (char)0xC1; 
      else target += (char)n2; 
    }
  }
  return target;
}

// Помощник для ввода цифр с пульта Car MP3
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

// ---------------- Логика ----------------

void showPopup(String msg) {
    popupMsg = msg;
    popupTimer = millis();
}

void toggleLamp() {
    lampState = !lampState;
    digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
    showPopup(lampState ? "Свет ВКЛ" : "Свет ВЫКЛ");
}

void initSystem() {
    // Разное время для будильников
    alarms[0] = {7, 0, ALARM_OFF, SOUND_BEEP, LIGHT_BLINK, false};
    alarms[1] = {8, 30, ALARM_OFF, SOUND_BIRDS, LIGHT_STATIC, false};
    alarms[2] = {9, 0, ALARM_OFF, SOUND_SIREN, LIGHT_OFF, false};
    
    for(int i=0; i<MAX_PARTICLES; i++) {
        particles[i] = {random(0,160), random(-80, 0), random(1,3), true};
    }
}

void updateTime() {
    unsigned long now = millis();
    if (now - rtc.lastMillis >= 1000) {
        rtc.second++;
        rtc.lastMillis = now;
        if (rtc.second >= 60) { rtc.second = 0; rtc.minute++; }
        if (rtc.minute >= 60) { rtc.minute = 0; rtc.hour++; }
        if (rtc.hour >= 24) { 
            rtc.hour = 0; rtc.day++; rtc.dayOfWeek++; 
            if(rtc.dayOfWeek > 7) rtc.dayOfWeek = 1;
            if(rtc.day > 30) { rtc.day = 1; rtc.month++; }
            if(rtc.month > 12) { rtc.month = 1; rtc.year++; }
        }
    }
}

void updateThemeAndSensors() {
    if (millis() - lastSensor > 2000) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if(!isnan(t)) temp = t;
        if(!isnan(h)) hum = h;
        lastSensor = millis();

        if (rtc.hour >= 22 || rtc.hour < 7) currentTheme = &thNight;
        else {
            if (rtc.month >= 12 || rtc.month <= 2) currentTheme = &thWinter;
            else if (rtc.month >= 3 && rtc.month <= 5) currentTheme = &thSpring;
            else if (rtc.month >= 6 && rtc.month <= 8) currentTheme = &thSummer;
            else currentTheme = &thAutumn;
        }
        
        if (temp > 28) eyeState = 2; 
        else if (rtc.hour >= 23 || rtc.hour < 6) eyeState = 3; 
        else eyeState = 0; 
    }
}

void updateWeatherEffect() {
    if (currentMode != MODE_CLOCK && currentMode != MODE_TUTORIAL) return;
    
    uint16_t pColor = (temp < 0 || rtc.month == 12 || rtc.month <= 2) ? ST7735_WHITE : ST7735_BLUE;
    bool isRain = (pColor == ST7735_BLUE);

    for(int i=0; i<MAX_PARTICLES; i++) {
        if (particles[i].active) {
            particles[i].y += particles[i].speed;
            if (particles[i].y > 80) {
                particles[i].y = random(-20, 0);
                particles[i].x = random(0, 160);
            }
            if (isRain) canvas.drawFastVLine(particles[i].x, particles[i].y, 3, pColor);
            else canvas.drawPixel(particles[i].x, particles[i].y, pColor);
        }
    }
}

void checkAlarms() {
    if (rtc.second != 0) return; 
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
            if (alarms[i].light != LIGHT_OFF) {
                lampState = true;
                digitalWrite(PIN_RELAY, HIGH);
            }
            if (alarms[i].type == ALARM_ONCE) alarms[i].active = false;
        }
    }
}

void playAlarmSound() {
    if (millis() - soundTimer > 200) { 
        soundTimer = millis();
        soundState = !soundState;
        
        AlarmSound s = alarms[currentAlarmIdx].sound;
        if (s == SOUND_BEEP) {
            if (soundState) tone(PIN_BUZZER, 1000, 100); else noTone(PIN_BUZZER);
        } else if (s == SOUND_SIREN) {
            if (soundState) tone(PIN_BUZZER, 600, 150); else tone(PIN_BUZZER, 1200, 150);
        } else { 
            if (soundState) { tone(PIN_BUZZER, 2000, 50); delay(50); tone(PIN_BUZZER, 2500, 50); }
        }

        if (alarms[currentAlarmIdx].light == LIGHT_BLINK) {
            lampState = !lampState;
            digitalWrite(PIN_RELAY, lampState);
        }
    }
}

// ---------------- Отрисовка ----------------

void drawGradient() {
    uint8_t r1 = (currentTheme->top >> 11) & 0x1F, g1 = (currentTheme->top >> 5) & 0x3F, b1 = currentTheme->top & 0x1F;
    uint8_t r2 = (currentTheme->bot >> 11) & 0x1F, g2 = (currentTheme->bot >> 5) & 0x3F, b2 = currentTheme->bot & 0x1F;
    for (int y = 0; y < 80; y++) {
        uint8_t r = r1 + (r2 - r1) * y / 80;
        uint8_t g = g1 + (g2 - g1) * y / 80;
        uint8_t b = b1 + (b2 - b1) * y / 80;
        canvas.drawFastHLine(0, y, 160, (r << 11) | (g << 5) | b);
    }
}

void drawEyes(int x_offset) {
    int x1 = 40 + x_offset, y1 = 40;
    int x2 = 120 + x_offset, y2 = 40;
    int r = 15;
    
    if (millis() > nextBlink) {
        canvas.drawLine(x1-15, y1, x1+15, y1, ST7735_WHITE);
        canvas.drawLine(x2-15, y2, x2+15, y2, ST7735_WHITE);
        if (millis() > nextBlink + 150) nextBlink = millis() + random(2000, 5000);
        return;
    }

    canvas.fillCircle(x1, y1, r, ST7735_WHITE);
    canvas.fillCircle(x2, y2, r, ST7735_WHITE);
    canvas.fillCircle(x1 + pupilX, y1 + pupilY, 5, ST7735_BLACK);
    canvas.fillCircle(x2 + pupilX, y2 + pupilY, 5, ST7735_BLACK);

    if (eyeState == 2) { 
        canvas.fillTriangle(x1-15, y1-18, x1+15, y1-8, x1, y1-5, currentTheme->top);
        canvas.fillTriangle(x2-15, y2-8, x2+15, y2-18, x2, y2-5, currentTheme->top);
    } else if (eyeState == 3) { 
        canvas.fillRect(x1-15, y1-15, 30, 10, currentTheme->top);
        canvas.fillRect(x2-15, y2-15, 30, 10, currentTheme->top);
    }
}

void drawListItem(int y, String text, bool selected) {
    if (selected) {
        canvas.fillRoundRect(5, y, 150, 18, 4, currentTheme->accent);
        canvas.setTextColor(ST7735_BLACK);
    } else {
        // Прозрачный фон или цвет панели
        canvas.setTextColor(currentTheme->text);
    }
    canvas.setCursor(15, y + 13);
    canvas.print(utf8rus(text));
}

void drawScreenClock() {
    drawGradient();
    
    if (currentMode == MODE_CLOCK) { 
        drawEyes(0);
        updateWeatherEffect();

        canvas.setFont(FONT_TIME);
        canvas.setTextColor(currentTheme->text);
        char timeS[10];
        snprintf(timeS, 10, "%02d:%02d", rtc.hour, rtc.minute);
        int16_t x1, y1; uint16_t w, h;
        canvas.getTextBounds(timeS, 0, 0, &x1, &y1, &w, &h);
        canvas.setCursor((160-w)/2, 75); 
        canvas.print(timeS);

        canvas.setFont(FONT_TEXT);
        canvas.setTextColor(currentTheme->accent);
        canvas.setCursor(5, 15);
        canvas.print(String(rtc.day) + "." + String(rtc.month));
        
        canvas.setCursor(100, 15);
        canvas.print(String((int)temp) + "C " + String((int)hum) + "%");
    }
}

void drawMenu() {
    drawGradient();
    canvas.setFont(FONT_HEADER);
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor(60, 15); canvas.print(utf8rus("МЕНЮ"));

    canvas.setFont(FONT_TEXT);
    const char* items[] = {"Будильники", "Время/Дата", "Свет", "Обучение"};
    
    int start = (menuCursor > 2) ? menuCursor - 2 : 0;
    int y = 25;
    for(int i = 0; i < 3; i++) {
        int idx = start + i;
        if (idx >= 4) break;
        drawListItem(y, items[idx], idx == menuCursor);
        y += 20;
    }
}

// Экран настройки времени (с использованием tempRtc)
void drawSetTime() {
    canvas.fillScreen(ST7735_BLACK);
    canvas.setFont(FONT_HEADER);
    canvas.setTextColor(currentTheme->text);
    canvas.setCursor(30, 15); canvas.print(utf8rus("УСТАНОВКА"));
    
    // Подписи
    canvas.setFont(FONT_TEXT);
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor(10, 35); canvas.print(utf8rus("Время:"));
    canvas.setCursor(10, 60); canvas.print(utf8rus("Дата:"));

    // Значения
    int yT = 35, yD = 60;
    int x = 60;

    // Часы
    canvas.setTextColor(settingField == 0 ? currentTheme->accent : currentTheme->text);
    canvas.setCursor(x, yT); canvas.print(tempRtc.hour < 10 ? "0" : ""); canvas.print(tempRtc.hour);
    canvas.setTextColor(currentTheme->text); canvas.print(":");
    
    // Минуты
    x += 25;
    canvas.setTextColor(settingField == 1 ? currentTheme->accent : currentTheme->text);
    canvas.setCursor(x, yT); canvas.print(tempRtc.minute < 10 ? "0" : ""); canvas.print(tempRtc.minute);

    // День
    x = 60;
    canvas.setTextColor(settingField == 2 ? currentTheme->accent : currentTheme->text);
    canvas.setCursor(x, yD); canvas.print(tempRtc.day);
    canvas.setTextColor(currentTheme->text); canvas.print(".");
    
    // Месяц
    x += 20;
    canvas.setTextColor(settingField == 3 ? currentTheme->accent : currentTheme->text);
    canvas.setCursor(x, yD); canvas.print(tempRtc.month);
}

// Экран настройки будильника (с использованием tempAlarm)
void drawAlarmEdit() {
    canvas.fillScreen(ST7735_BLACK);
    canvas.setFont(FONT_HEADER);
    canvas.setTextColor(currentTheme->text);
    canvas.setCursor(5,15); canvas.print(utf8rus("Будильник ") + String(currentAlarmIdx+1));
    
    canvas.setFont(FONT_TEXT);
    int y = 30;
    
    // Поле 0: Время
    canvas.setCursor(5, y); canvas.print(utf8rus("Время: "));
    if(settingField==0) canvas.setTextColor(currentTheme->accent); else canvas.setTextColor(currentTheme->text);
    canvas.print(tempAlarm.hour); canvas.print(":");
    if(settingField==1) canvas.setTextColor(currentTheme->accent); else canvas.setTextColor(currentTheme->text);
    canvas.print(tempAlarm.minute < 10 ? "0" : ""); canvas.print(tempAlarm.minute);
    
    y += 15;
    // Поле 2: Режим
    canvas.setTextColor(currentTheme->text); canvas.setCursor(5, y); canvas.print(utf8rus("Режим: "));
    if(settingField==2) canvas.setTextColor(currentTheme->accent); else canvas.setTextColor(currentTheme->text);
    String types[] = {"ВЫКЛ", "1 Раз", "Ежедн", "Будни"};
    canvas.print(utf8rus(types[tempAlarm.type]));

    y += 15;
    // Поле 3: Звук
    canvas.setTextColor(currentTheme->text); canvas.setCursor(5, y); canvas.print(utf8rus("Звук: "));
    if(settingField==3) canvas.setTextColor(currentTheme->accent); else canvas.setTextColor(currentTheme->text);
    String sounds[] = {"Бип", "Птицы", "Сирена"};
    canvas.print(utf8rus(sounds[tempAlarm.sound]));
}

void drawTutorial() {
    canvas.fillScreen(RGB(20,20,20));
    canvas.setFont(FONT_TEXT);
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor(10, 20);
    
    String txt = "";
    switch(tutorialStep) {
        case 0: txt = "Привет! Я Часы.\nЖми NEXT >"; break;
        case 1: txt = "EQ - Меню\nCH - Домой"; break;
        case 2: txt = "CH- - Лампа\nCH+ - Цитата"; break;
        case 3: txt = "100+ - ОК\n200+ - Сброс"; break;
        case 4: txt = "Готово!\nУдачи!"; break;
    }
    
    int y = 20;
    int lineStart = 0;
    for (int i=0; i<txt.length(); i++) {
        if(txt[i] == '\n') {
            canvas.setCursor(10, y);
            canvas.print(utf8rus(txt.substring(lineStart, i)));
            y+=20; lineStart = i+1;
        }
    }
    canvas.setCursor(10, y);
    canvas.print(utf8rus(txt.substring(lineStart)));
}

void drawRing() {
    if ((millis()/300)%2 == 0) canvas.fillScreen(ST7735_RED); 
    else canvas.fillScreen(ST7735_BLACK);
    
    canvas.setFont(FONT_TIME);
    canvas.setTextColor(ST7735_WHITE);
    canvas.setCursor(15, 50);
    canvas.print(utf8rus("ПОДЪЕМ!"));
}

void drawQuotesScreen() {
    drawGradient();
    canvas.setFont(FONT_TEXT);
    canvas.setTextColor(currentTheme->text);
    canvas.setCursor(5, 30);
    canvas.print(utf8rus(quotes[quoteIdx]));
    canvas.drawRoundRect(2, 10, 156, 50, 5, currentTheme->accent);
}

// ---------------- Обработка ввода (IR) ----------------

void handleIR() {
    if (!irrecv.decode(&results)) return;
    unsigned long key = results.value;
    irrecv.resume(); 
    delay(100); 

    int digit = getDigitFromKey(key); // Получаем цифру, если нажата

    if (key == IR_BTN_CH_MINUS) { 
        toggleLamp();
        return;
    }
    
    if (currentMode == MODE_RING) { 
        currentMode = MODE_CLOCK;
        noTone(PIN_BUZZER);
        if (alarms[currentAlarmIdx].light == LIGHT_BLINK) digitalWrite(PIN_RELAY, LOW);
        return;
    }

    if (currentMode == MODE_TUTORIAL) {
        if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) tutorialStep++;
        if (key == IR_BTN_PREV || key == IR_BTN_MINUS) tutorialStep--;
        if (key == IR_BTN_EQ || tutorialStep > 4) { currentMode = MODE_CLOCK; showPopup("Конец"); }
        if (tutorialStep < 0) tutorialStep = 0;
        return;
    }

    switch (currentMode) {
        case MODE_CLOCK:
            if (key == IR_BTN_EQ) { currentMode = MODE_MENU; menuCursor = 0; }
            if (key == IR_BTN_CH_PLUS) { currentMode = MODE_QUOTES; quoteIdx = random(0, 7); }
            if (key == IR_BTN_200PLUS) { 
                for(int i=0; i<MAX_ALARMS; i++) alarms[i].active = false;
                showPopup("Все откл.");
            }
            break;

        case MODE_MENU:
            if (key == IR_BTN_EQ) currentMode = MODE_CLOCK;
            if (key == IR_BTN_PREV || key == IR_BTN_MINUS) { menuCursor--; if (menuCursor < 0) menuCursor = 3; }
            if (key == IR_BTN_NEXT || key == IR_BTN_PLUS) { menuCursor++; if (menuCursor > 3) menuCursor = 0; }
            if (key == IR_BTN_100PLUS) { 
                if (menuCursor == 0) { currentMode = MODE_ALARMS_LIST; menuCursor = 0; }
                if (menuCursor == 1) { 
                    currentMode = MODE_SET_TIME; 
                    settingField = 0; 
                    tempRtc = rtc; // Копируем во временную структуру
                }
                if (menuCursor == 2) { toggleLamp(); }
                if (menuCursor == 3) { currentMode = MODE_TUTORIAL; tutorialStep = 0; }
            }
            break;

        case MODE_SET_TIME: 
            if (key == IR_BTN_EQ) { currentMode = MODE_CLOCK; showPopup("Отмена"); }
            if (key == IR_BTN_100PLUS) { 
                rtc = tempRtc; // Сохраняем только по ОК
                currentMode = MODE_CLOCK; 
                showPopup("Сохранено"); 
            }
            // Переход по полям
            if (key == IR_BTN_PREV) settingField = (settingField - 1 + 4) % 4;
            if (key == IR_BTN_NEXT) settingField = (settingField + 1) % 4;
            
            // Изменение значений
            if (key == IR_BTN_PLUS) {
                if(settingField==0) tempRtc.hour = (tempRtc.hour+1)%24;
                if(settingField==1) tempRtc.minute = (tempRtc.minute+1)%60;
                if(settingField==2) tempRtc.day = (tempRtc.day%31)+1;
                if(settingField==3) tempRtc.month = (tempRtc.month%12)+1;
            }
            if (key == IR_BTN_MINUS) {
                if(settingField==0) tempRtc.hour = (tempRtc.hour+23)%24;
                if(settingField==1) tempRtc.minute = (tempRtc.minute+59)%60;
                // ...остальное аналогично
            }
            // Цифровой ввод
            if (digit != -1) {
                if(settingField==0) tempRtc.hour = digit; // Для простоты вводим одну цифру, можно усложнить
                if(settingField==1) tempRtc.minute = digit; 
                if(settingField==2) tempRtc.day = digit; 
                if(settingField==3) tempRtc.month = digit; 
            }
            break;

        case MODE_ALARMS_LIST: 
            if (key == IR_BTN_EQ) currentMode = MODE_MENU;
            if (key == IR_BTN_PREV) { menuCursor--; if(menuCursor<0) menuCursor = MAX_ALARMS-1; }
            if (key == IR_BTN_NEXT) { menuCursor++; if(menuCursor>=MAX_ALARMS) menuCursor = 0; }
            if (key == IR_BTN_100PLUS) { 
                currentAlarmIdx = menuCursor; 
                tempAlarm = alarms[currentAlarmIdx]; // Копируем в буфер
                currentMode = MODE_ALARM_EDIT; 
                settingField = 0; 
            }
            break;

        case MODE_ALARM_EDIT: 
            if (key == IR_BTN_EQ) { currentMode = MODE_ALARMS_LIST; showPopup("Отмена"); }
            if (key == IR_BTN_100PLUS) {
                alarms[currentAlarmIdx] = tempAlarm; // Сохраняем
                alarms[currentAlarmIdx].active = (tempAlarm.type != ALARM_OFF);
                currentMode = MODE_ALARMS_LIST;
                showPopup("Сохранено");
            }
            
            if (key == IR_BTN_PREV) settingField = (settingField-1+4)%4;
            if (key == IR_BTN_NEXT) settingField = (settingField+1)%4;
            
            if (key == IR_BTN_PLUS) {
                if(settingField==0) tempAlarm.hour = (tempAlarm.hour+1)%24;
                if(settingField==1) tempAlarm.minute = (tempAlarm.minute+1)%60;
                if(settingField==2) { 
                    int t = (int)tempAlarm.type + 1; if(t > 3) t = 0; tempAlarm.type = (AlarmType)t; 
                }
                if(settingField==3) { 
                    int s = (int)tempAlarm.sound + 1; if(s > 2) s = 0; tempAlarm.sound = (AlarmSound)s; 
                }
            }
            
            if (digit != -1) {
                if(settingField==0) tempAlarm.hour = digit;
                if(settingField==1) tempAlarm.minute = digit;
            }
            
            if (key == IR_BTN_200PLUS) {
                alarms[currentAlarmIdx].type = ALARM_OFF;
                alarms[currentAlarmIdx].active = false;
                currentMode = MODE_ALARMS_LIST;
                showPopup("Удален");
            }
            break;
            
        case MODE_QUOTES:
            if (key == IR_BTN_CH || key == IR_BTN_EQ) currentMode = MODE_CLOCK;
            if (key == IR_BTN_NEXT) { quoteIdx++; if(quoteIdx >= 7) quoteIdx = 0; }
            break;
    }
}

// ---------------- Main ----------------

void setup() {
    pinMode(PIN_RELAY, OUTPUT); digitalWrite(PIN_RELAY, LOW);
    pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);

    dht.begin();
    irrecv.enableIRIn();

    tft.initR(INITR_MINI160x80); 
    tft.setRotation(3); 
    
    initSystem();
    updateThemeAndSensors();
    
    currentMode = MODE_TUTORIAL; 
}

void loop() {
    updateTime();
    updateThemeAndSensors();
    checkAlarms();
    handleIR();

    if (currentMode == MODE_RING) playAlarmSound();

    if (millis() - lastDraw > 50) {
        switch (currentMode) {
            case MODE_CLOCK: drawScreenClock(); break;
            case MODE_MENU: drawMenu(); break;
            case MODE_SET_TIME: drawSetTime(); break;
            case MODE_ALARMS_LIST: 
                drawGradient(); 
                canvas.setFont(FONT_TEXT); 
                for(int i=0; i<MAX_ALARMS; i++) {
                    String s = String(alarms[i].hour)+":"+(alarms[i].minute<10?"0":"")+String(alarms[i].minute);
                    if(alarms[i].type == ALARM_OFF) s += " ВЫКЛ";
                    else if(alarms[i].active) s += " ВКЛ";
                    drawListItem(5 + i*24, s, i==menuCursor);
                }
                break;
            case MODE_ALARM_EDIT: drawAlarmEdit(); break;
            case MODE_TUTORIAL: drawTutorial(); break;
            case MODE_QUOTES: drawQuotesScreen(); break;
            case MODE_RING: drawRing(); break;
        }

        if(millis() - popupTimer < 2000 && popupMsg != "") {
            canvas.fillRoundRect(30, 30, 100, 20, 5, currentTheme->accent);
            canvas.setTextColor(ST7735_BLACK);
            canvas.setFont(FONT_TEXT);
            canvas.setCursor(35, 43); // Скорректировано под 9pt
            canvas.print(utf8rus(popupMsg));
        }

        tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 80);
        lastDraw = millis();
    }
}