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
// Убедись, что имена файлов точно совпадают с теми, что в папке
#include "fonts/FontsRus/FreeSansBold14.h"
#include "fonts/FontsRus/FreeSansBold9.h"// Используем 9pt для мелкого текста

// ВАЖНО: В библиотеке FontsRus имена объектов шрифтов могут отличаться.
// Судя по вашим файлам, они называются так:
#define FONT_LARGE &FreeSansBold14pt8b
#define FONT_SMALL &FreeSansBold9pt8b // Если компилятор ругается, проверьте имя внутри FreeSans9.h (может быть FreeSansBold9pt8b)

// ---------------- Объекты ----------------
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);
GFXcanvas16 canvas(160, 80); // Буфер кадра

// ----------------- ПРАВИЛЬНАЯ Функция русификации ------------------
// Специально для шрифтов FontsRus (immortalserg)
// Карта символов: 0x80-0x8F (р-я), 0x90-0xBF (А-п), 0xC0(Ё), 0xC1(ё)
String utf8rus(String source) {
  String target = "";
  int length = source.length();
  
  for (int i = 0; i < length; i++) {
    unsigned char n = source[i];
    
    if (n < 127) {
      // Английский и символы - без изменений
      target += (char)n;
    } 
    else if (n == 0xD0 && i + 1 < length) { // Первый байт UTF-8 для 'А'-'п' и 'Ё'
      unsigned char n2 = source[++i];
      if (n2 == 0x81) {
        target += (char)0xC0; // Ё -> индекс 192
      } else {
        target += (char)n2;   // А-п -> индексы 144-191 (совпадают со вторым байтом UTF)
      }
    } 
    else if (n == 0xD1 && i + 1 < length) { // Первый байт UTF-8 для 'р'-'я' и 'ё'
      unsigned char n2 = source[++i];
      if (n2 == 0x91) {
        target += (char)0xC1; // ё -> индекс 193
      } else {
        target += (char)n2;   // р-я -> индексы 128-143 (совпадают со вторым байтом UTF)
      }
    }
  }
  return target;
}

// ----------------- Цвета и Темы ------------------
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

struct Theme {
    uint16_t bgTop;      
    uint16_t bgBottom;   
    uint16_t textMain;   
    uint16_t textSec;    
    uint16_t accent;     
    uint16_t accentBg;   
};

Theme themeNight  = { RGB(10, 10, 30),   RGB(0, 0, 0),       RGB(200, 200, 200), RGB(100, 100, 100), RGB(255, 200, 0),   RGB(50, 50, 70) };
Theme themeWinter = { RGB(40, 60, 120),  RGB(200, 220, 255), RGB(255, 255, 255), RGB(180, 200, 255), RGB(0, 255, 255),   RGB(20, 40, 90) };
Theme themeSpring = { RGB(40, 100, 40),  RGB(150, 240, 100), RGB(255, 255, 240), RGB(200, 255, 200), RGB(255, 100, 150), RGB(20, 70, 20) };
Theme themeSummer = { RGB(0, 140, 240),  RGB(255, 210, 80),  RGB(255, 255, 255), RGB(255, 255, 200), RGB(255, 140, 0),   RGB(0, 80, 180) };
Theme themeAutumn = { RGB(120, 60, 0),   RGB(255, 160, 0),   RGB(255, 240, 200), RGB(255, 210, 150), RGB(255, 215, 0),   RGB(90, 40, 0) };

Theme* currentTheme = &themeNight;

// ---------------- Глобальные переменные ----------------
enum AppMode {MODE_CLOCK, MODE_ANIMATION, MODE_SETTINGS, MODE_ALARM_RING, MODE_MENU, MODE_QUOTES};
AppMode currentMode = MODE_CLOCK;

bool lampState = false;
float temperature = 0;
float humidity = 0;

#define MAX_ALARMS 5
struct Alarm { bool enabled; uint8_t hour; uint8_t minute; };
Alarm alarms[MAX_ALARMS] = {{false, 7, 0}, {false, 8, 0}, {false, 9, 0}, {false, 0, 0}, {false, 0, 0}};

int settingCursor = 0; // 0=Часы, 1=Минуты, 2=Месяц
int menuCursor = 0;
const int MENU_ITEMS_COUNT = 5;
const char* menuItems[] = {"Будильники", "Свет", "Эффекты", "Цитаты", "Звук"};

int quotePage = 0;

struct SoftClock {
    int hour;
    int minute;
    int second;
    int month;
    unsigned long baseMillis;
} clockTime;

// Анимация глаз
long nextBlink = 0;
bool isBlinking = false;
int pupilX = 0, pupilY = 0;

// Popup
String popupMsg = "";
unsigned long popupTimer = 0;

unsigned long lastSensorRead = 0;
unsigned long lastDraw = 0;

const char* quotes[] = {
    "Be happy!",
    "Доброе утро!",
    "Улыбнись!",
    "Ты супер!",
    "Never give up!"
};
#define QUOTES_COUNT 5

// ---------------- Функции логики ----------------

void updateAutoTheme() {
    if (clockTime.hour >= 22 || clockTime.hour < 6) {
        currentTheme = &themeNight;
    } else {
        if (clockTime.month == 12 || clockTime.month <= 2) currentTheme = &themeWinter;
        else if (clockTime.month >= 3 && clockTime.month <= 5) currentTheme = &themeSpring;
        else if (clockTime.month >= 6 && clockTime.month <= 8) currentTheme = &themeSummer;
        else currentTheme = &themeAutumn;
    }
}

void drawGradientBg() {
    uint16_t c1 = currentTheme->bgTop;
    uint16_t c2 = currentTheme->bgBottom;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;

    for (int y = 0; y < 80; y++) {
        uint8_t r = r1 + (r2 - r1) * y / 80;
        uint8_t g = g1 + (g2 - g1) * y / 80;
        uint8_t b = b1 + (b2 - b1) * y / 80;
        canvas.drawFastHLine(0, y, 160, (r << 11) | (g << 5) | b);
    }
}

void toggleLamp() {
    lampState = !lampState;
    digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
}

void showPopup(String msg) {
    popupMsg = msg;
    popupTimer = millis();
}

void resetAllAlarms() {
    for(int i = 0; i < MAX_ALARMS; i++) alarms[i].enabled = false;
    showPopup("Alarms OFF");
}

void processMenuAction() {
    switch(menuCursor) {
        case 0: currentMode = MODE_SETTINGS; settingCursor = 0; break;
        case 1: toggleLamp(); showPopup(lampState ? "Light ON" : "Light OFF"); break;
        case 2: currentMode = MODE_ANIMATION; break;
        case 3: currentMode = MODE_QUOTES; quotePage = 0; break;
        case 4: showPopup("Sound Menu"); break;
    }
    if (currentMode != MODE_SETTINGS && currentMode != MODE_QUOTES && currentMode != MODE_ANIMATION) {
        currentMode = MODE_CLOCK;
    }
}

void updateSensors() {
    if (millis() - lastSensorRead > 2000) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (!isnan(t)) temperature = t;
        if (!isnan(h)) humidity = h;
        lastSensorRead = millis();
    }
}

void updateClock() {
    unsigned long delta = (millis() - clockTime.baseMillis) / 1000;
    clockTime.baseMillis += delta * 1000;

    clockTime.second += delta;
    if (clockTime.second >= 60) { clockTime.second %= 60; clockTime.minute++; }
    if (clockTime.minute >= 60) { clockTime.minute = 0; clockTime.hour++; }
    if (clockTime.hour >= 24) { clockTime.hour = 0; }
    
    static int lastMinCheck = -1;
    if (clockTime.minute != lastMinCheck) {
        updateAutoTheme();
        lastMinCheck = clockTime.minute;
    }
}

// ---------------- Отрисовка ----------------

void drawClock() {
    drawGradientBg();
    
    // --- Время ---
    canvas.setFont(FONT_LARGE);
    canvas.setTextColor(currentTheme->textMain);
    
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", clockTime.hour, clockTime.minute);
    
    int16_t x1, y1; uint16_t w, h;
    canvas.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((160 - w) / 2, 45);
    canvas.print(timeStr);

    // --- Инфо строка ---
    canvas.setFont(FONT_SMALL);
    canvas.setTextColor(currentTheme->textSec);
    char dataStr[30];
    snprintf(dataStr, sizeof(dataStr), "%.0fC  %.0f%%", temperature, humidity);
    
    canvas.getTextBounds(dataStr, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((160 - w) / 2, 72);
    canvas.print(dataStr);
    
    // --- Индикаторы ---
    canvas.setFont(); 
    bool hasActiveAlarms = false;
    for(int i = 0; i < MAX_ALARMS; i++) if(alarms[i].enabled) hasActiveAlarms = true;
    
    if(hasActiveAlarms) canvas.fillCircle(10, 10, 3, RGB(0, 255, 0));
    if(lampState) canvas.fillCircle(150, 10, 4, currentTheme->accent);

    // Popup
    if(millis() - popupTimer < 2000 && popupMsg != "") {
        canvas.fillRoundRect(20, 25, 120, 30, 5, currentTheme->accentBg);
        canvas.setTextColor(currentTheme->textMain);
        canvas.setFont(NULL); // Используем стандартный шрифт для попапа (чтобы влезло)
        canvas.setCursor(30, 35);
        canvas.print(utf8rus(popupMsg));
    }
}

void drawEyes() {
    canvas.fillScreen(currentTheme->bgTop);
    uint16_t eyeColor = ST7735_WHITE, pupilColor = ST7735_BLACK;
    if (temperature > 28) { eyeColor = ST7735_YELLOW; pupilColor = ST7735_RED; }

    if (millis() > nextBlink) { isBlinking = true; if (millis() > nextBlink + 150) { isBlinking = false; nextBlink = millis() + random(1000,4000); pupilX = random(-10,10); pupilY = random(-5,5); } }

    if (isBlinking) {
        canvas.drawLine(25,40,55,40,eyeColor);
        canvas.drawLine(105,40,135,40,eyeColor);
    } else {
        canvas.fillCircle(40,40,15,eyeColor);
        canvas.fillCircle(120,40,15,eyeColor);
        canvas.fillCircle(40+pupilX,40+pupilY,5,pupilColor);
        canvas.fillCircle(120+pupilX,40+pupilY,5,pupilColor);
    }
}

void drawSettings() {
    drawGradientBg();
    canvas.setFont(FONT_SMALL);
    canvas.setTextColor(currentTheme->textMain);
    canvas.setCursor(20,20); canvas.print(utf8rus("НАСТРОЙКИ"));

    char buf[10];
    int y = 55;
    
    // Часы
    canvas.setFont(FONT_LARGE);
    canvas.setTextColor(settingCursor == 0 ? currentTheme->accent : currentTheme->textMain);
    snprintf(buf,5,"%02d",clockTime.hour);
    canvas.setCursor(15,y); canvas.print(buf);

    canvas.setTextColor(currentTheme->textMain); canvas.print(":");

    // Минуты
    canvas.setTextColor(settingCursor == 1 ? currentTheme->accent : currentTheme->textMain);
    snprintf(buf,5,"%02d",clockTime.minute);
    canvas.setCursor(65,y); canvas.print(buf);
    
    // Месяц
    canvas.setFont(FONT_SMALL);
    canvas.setTextColor(currentTheme->textMain);
    canvas.setCursor(115, 35); canvas.print(utf8rus("Мес:"));
    
    canvas.setTextColor(settingCursor == 2 ? currentTheme->accent : currentTheme->textMain);
    canvas.setCursor(125, y); 
    canvas.print(clockTime.month);
}

void drawMenuScrollable() {
    drawGradientBg();
    canvas.setFont(FONT_SMALL);
    
    int visibleItems = 3;
    int startIdx = menuCursor - 1;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > MENU_ITEMS_COUNT - visibleItems) startIdx = MENU_ITEMS_COUNT - visibleItems;

    int y = 25; 
    
    for (int i = 0; i < visibleItems; i++) {
        int itemIndex = startIdx + i;
        if (itemIndex >= MENU_ITEMS_COUNT) break;

        if (itemIndex == menuCursor) {
            canvas.fillRoundRect(5, y - 16, 150, 22, 5, currentTheme->accentBg);
            canvas.setTextColor(currentTheme->accent);
        } else {
            canvas.setTextColor(currentTheme->textMain);
        }
        
        canvas.setCursor(20, y);
        canvas.print(utf8rus(menuItems[itemIndex]));
        y += 24;
    }
}

void drawQuotes() {
    canvas.fillScreen(ST7735_BLACK);
    canvas.setFont(FONT_SMALL);
    
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor(5,15);
    canvas.print(utf8rus("Цитата:"));
    
    canvas.setTextColor(ST7735_WHITE);
    canvas.setCursor(5, 45);
    canvas.print(utf8rus(quotes[quotePage])); 
}

void handleAlarmRing() {
    if ((millis()/500)%2==0) canvas.fillScreen(ST7735_RED); else canvas.fillScreen(ST7735_BLACK);
    canvas.setFont(FONT_LARGE);
    canvas.setTextColor(ST7735_WHITE); 
    canvas.setCursor(10,50); canvas.print("WAKE UP!");
    if ((millis()/200)%2==0) digitalWrite(PIN_BUZZER,HIGH); else digitalWrite(PIN_BUZZER,LOW);
}

void handleDigitInput(int digit) {
    if (currentMode != MODE_SETTINGS) return;
    switch(settingCursor) {
        case 0: { int h = clockTime.hour; if(h<10) h=h*10+digit; else h=digit; if(h>23) h=digit; clockTime.hour=h; break; }
        case 1: { int m = clockTime.minute; if(m<10) m=m*10+digit; else m=digit; if(m>59) m=digit; clockTime.minute=m; break; }
        case 2: { int m = clockTime.month; if(m<10 && m!=0) m=m*10+digit; else m=digit; if(m>12 || m<1) m=digit; if(m==0) m=1; clockTime.month=m; break; }
    }
}

// ---------------- Обработка ИК ----------------
void handleIR() {
    if(!irrecv.decode(&results)) return;
    unsigned long key = results.value;
    
    if(key == IR_BTN_CH_MINUS) { toggleLamp(); irrecv.resume(); return; }
    if(currentMode == MODE_ALARM_RING) {
        currentMode = MODE_CLOCK; digitalWrite(PIN_BUZZER, LOW); digitalWrite(PIN_RELAY, LOW); lampState = false;
        irrecv.resume(); return;
    }

    switch(currentMode) {
        case MODE_CLOCK:
        case MODE_ANIMATION:
            if(key == IR_BTN_EQ) { currentMode = MODE_MENU; menuCursor = 0; }
            else if(key == IR_BTN_CH) currentMode = MODE_CLOCK;
            else if(key == IR_BTN_CH_PLUS) { currentMode = MODE_QUOTES; quotePage = 0; }
            else if(key == IR_BTN_PLAY) currentMode = (currentMode == MODE_CLOCK) ? MODE_ANIMATION : MODE_CLOCK;
            else if(key == IR_BTN_5) { alarms[0] = {true, 5, 0}; showPopup("Set 5:00"); }
            else if(key == IR_BTN_7) { alarms[0] = {true, 7, 0}; showPopup("Set 7:00"); }
            else if(key == IR_BTN_100PLUS) { currentMode = MODE_MENU; menuCursor = 0; }
            else if(key == IR_BTN_200PLUS) resetAllAlarms();
            break;

        case MODE_SETTINGS:
            if(key == IR_BTN_EQ) { currentMode = MODE_CLOCK; showPopup("Saved"); updateAutoTheme(); }
            else if(key == IR_BTN_PREV) { settingCursor--; if(settingCursor < 0) settingCursor = 2; }
            else if(key == IR_BTN_NEXT) { settingCursor++; if(settingCursor > 2) settingCursor = 0; }
            else if(key == IR_BTN_PLUS) {
                if(settingCursor==0) clockTime.hour = (clockTime.hour + 1) % 24;
                else if(settingCursor==1) clockTime.minute = (clockTime.minute + 1) % 60;
                else { clockTime.month++; if(clockTime.month > 12) clockTime.month = 1; }
            }
            else if(key == IR_BTN_MINUS) {
                if(settingCursor==0) clockTime.hour = (clockTime.hour - 1 + 24) % 24;
                else if(settingCursor==1) clockTime.minute = (clockTime.minute - 1 + 60) % 60;
                else { clockTime.month--; if(clockTime.month < 1) clockTime.month = 12; }
            }
            else if(key >= IR_BTN_0 && key <= IR_BTN_9) {
                handleDigitInput((key == IR_BTN_0) ? 0 : (key - IR_BTN_1 + 1));
            }
            break;

        case MODE_MENU:
            if(key == IR_BTN_100PLUS) processMenuAction();
            else if(key == IR_BTN_EQ) currentMode = MODE_CLOCK; 
            else if(key == IR_BTN_PREV || key == IR_BTN_MINUS) {
                menuCursor--; if(menuCursor < 0) menuCursor = MENU_ITEMS_COUNT - 1;
            }
            else if(key == IR_BTN_NEXT || key == IR_BTN_PLUS) {
                menuCursor++; if(menuCursor >= MENU_ITEMS_COUNT) menuCursor = 0;
            }
            break;

        case MODE_QUOTES:
            if(key == IR_BTN_EQ || key == IR_BTN_CH) currentMode = MODE_CLOCK;
            else if(key == IR_BTN_PREV || key == IR_BTN_MINUS) {
                quotePage--; if(quotePage < 0) quotePage = QUOTES_COUNT - 1;
            }
            else if(key == IR_BTN_NEXT || key == IR_BTN_PLUS) {
                quotePage = (quotePage + 1) % QUOTES_COUNT;
            }
            break;
    }
    irrecv.resume();
    delay(150);
}

// ---------------- Setup & Loop ----------------
void setup() {
    pinMode(PIN_RELAY,OUTPUT); digitalWrite(PIN_RELAY,LOW);
    pinMode(PIN_BUZZER,OUTPUT); digitalWrite(PIN_BUZZER,LOW);

    dht.begin();
    irrecv.enableIRIn();

    tft.initR(INITR_MINI160x80); 
    tft.setRotation(3); 
    tft.fillScreen(ST7735_BLACK);

    clockTime.hour=12; clockTime.minute=0; clockTime.second=0; 
    clockTime.month=1; 
    clockTime.baseMillis=millis();
    
    updateAutoTheme(); 
}

void loop() {
    unsigned long currentMillis = millis();

    updateClock();
    updateSensors();
    handleIR();

    for(int i = 0; i < MAX_ALARMS; i++) {
        if(alarms[i].enabled && clockTime.hour == alarms[i].hour && 
           clockTime.minute == alarms[i].minute && clockTime.second == 0) {
            currentMode = MODE_ALARM_RING;
            lampState = true;
            digitalWrite(PIN_RELAY, HIGH);
            break;
        }
    }

    if(currentMillis - lastDraw > 50) {
        switch(currentMode) {
            case MODE_CLOCK: drawClock(); break;
            case MODE_ANIMATION: drawEyes(); break;
            case MODE_SETTINGS: drawSettings(); break;
            case MODE_MENU: drawMenuScrollable(); break;
            case MODE_QUOTES: drawQuotes(); break;
            case MODE_ALARM_RING: handleAlarmRing(); break;
        }
        tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 80);
        lastDraw = currentMillis;
    }
}