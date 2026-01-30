#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h"

// --- Подключение шрифтов (Убедись, что имена объектов внутри файлов совпадают) ---
// Обычно внутри FreeSans14.h объект называется FreeSans14pt8b или подобным образом.
#include "fonts/FontsRus/FreeSans14.h"
#include "fonts/FontsRus/FreeSans9.h"

// ---------------- Объекты ----------------
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);
GFXcanvas16 canvas(160, 80); // Буфер кадра

// ----------------- Цвета и Темы ------------------
// RGB565 конвертер для удобства
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

struct Theme {
    uint16_t bgTop;      // Цвет градиента сверху
    uint16_t bgBottom;   // Цвет градиента снизу
    uint16_t textMain;   // Основной текст
    uint16_t textSec;    // Вторичный текст
    uint16_t accent;     // Акцент (курсор, иконки)
    uint16_t accentBg;   // Фон выделения
};

// Определяем темы
Theme themeNight  = { RGB(10, 10, 20),   RGB(0, 0, 0),       RGB(180, 180, 180), RGB(100, 100, 100), RGB(255, 200, 0),   RGB(40, 40, 50) };
Theme themeWinter = { RGB(50, 60, 100),  RGB(200, 220, 255), RGB(255, 255, 255), RGB(200, 200, 255), RGB(0, 255, 255),   RGB(30, 40, 80) };
Theme themeSpring = { RGB(50, 120, 50),  RGB(150, 255, 100), RGB(255, 255, 230), RGB(200, 255, 200), RGB(255, 100, 150), RGB(30, 80, 30) };
Theme themeSummer = { RGB(0, 150, 255),  RGB(255, 220, 100), RGB(255, 255, 255), RGB(255, 255, 200), RGB(255, 165, 0),   RGB(0, 100, 200) };
Theme themeAutumn = { RGB(100, 50, 0),   RGB(255, 150, 0),   RGB(255, 230, 200), RGB(255, 200, 150), RGB(255, 215, 0),   RGB(80, 40, 0) };

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

int settingCursor = 0;
int menuCursor = 0;
const int MENU_ITEMS_COUNT = 5;
const char* menuItems[] = {"Будильники", "Свет", "Эффекты", "Цитаты", "Звук"};

int quotePage = 0;

// Soft Clock (Добавили месяц для сезонности)
struct SoftClock {
    int hour;
    int minute;
    int second;
    int month; // 1-12
    unsigned long baseMillis;
} clockTime;

// Анимация глаз
long nextBlink = 0;
bool isBlinking = false;
int pupilX = 0, pupilY = 0;

// Popup
String popupMsg = "";
unsigned long popupTimer = 0;

// Таймеры
unsigned long lastSensorRead = 0;
unsigned long lastDraw = 0;

const char* quotes[] = {
    "Будь счастлив!",
    "Доброе утро!",
    "Хорошего дня!",
    "Улыбнись!",
    "Ты супер!"
};
#define QUOTES_COUNT 5

// ---------------- Вспомогательные функции ----------------

// Функция рисования градиента на холсте
void drawGradientBg() {
    uint16_t colorTop = currentTheme->bgTop;
    uint16_t colorBot = currentTheme->bgBottom;

    // Разбиваем на компоненты RGB
    uint8_t r1 = (colorTop >> 11) & 0x1F;
    uint8_t g1 = (colorTop >> 5) & 0x3F;
    uint8_t b1 = colorTop & 0x1F;

    uint8_t r2 = (colorBot >> 11) & 0x1F;
    uint8_t g2 = (colorBot >> 5) & 0x3F;
    uint8_t b2 = colorBot & 0x1F;

    // Рисуем линии
    for (int y = 0; y < 80; y++) {
        uint8_t r = r1 + (r2 - r1) * y / 80;
        uint8_t g = g1 + (g2 - g1) * y / 80;
        uint8_t b = b1 + (b2 - b1) * y / 80;
        uint16_t color = (r << 11) | (g << 5) | b;
        canvas.drawFastHLine(0, y, 160, color);
    }
}

// Автоматический выбор темы
void updateAutoTheme() {
    // Если время от 22:00 до 06:00 -> Ночная тема
    if (clockTime.hour >= 22 || clockTime.hour < 6) {
        currentTheme = &themeNight;
    } else {
        // Дневные сезонные темы
        if (clockTime.month == 12 || clockTime.month <= 2) currentTheme = &themeWinter;
        else if (clockTime.month >= 3 && clockTime.month <= 5) currentTheme = &themeSpring;
        else if (clockTime.month >= 6 && clockTime.month <= 8) currentTheme = &themeSummer;
        else currentTheme = &themeAutumn;
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
    showPopup("Все откл.");
}

void processMenuAction() {
    switch(menuCursor) {
        case 0: // Будильники
            currentMode = MODE_SETTINGS;
            settingCursor = 0;
            break;
        case 1: // Свет
            toggleLamp();
            showPopup(lampState ? "Свет ВКЛ" : "Свет ВЫКЛ");
            break;
        case 2: // Эффекты
            currentMode = MODE_ANIMATION;
            break;
        case 3: // Цитаты
            currentMode = MODE_QUOTES;
            quotePage = 0;
            break;
        case 4: // Звук
            showPopup("Меню звука");
            break;
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
    if (clockTime.hour >= 24) { 
        clockTime.hour = 0; 
        // Простая имитация смены дня (для тестов сезонов можно вручную менять clockTime.month)
    }
    
    // Проверка смены темы каждый час или при смене режима
    static int lastHourCheck = -1;
    if (clockTime.hour != lastHourCheck) {
        updateAutoTheme();
        lastHourCheck = clockTime.hour;
    }
}

// ---------------- Отрисовка ----------------

void drawClock() {
    drawGradientBg(); // Рисуем градиент

    // Эффекты погоды поверх градиента
    if (humidity > 70) {
        for (int i = 0; i < 5; i++) canvas.drawFastVLine(random(0,160), random(0,80), 3, RGB(100, 100, 255));
    }
    
    // --- Время (Большой шрифт) ---
    canvas.setFont(&FreeSans14pt8b); // Используем твой шрифт
    canvas.setTextColor(currentTheme->textMain);
    
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", clockTime.hour, clockTime.minute);
    
    // Центрирование текста
    int16_t x1, y1; uint16_t w, h;
    canvas.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((160 - w) / 2, 45);
    canvas.print(timeStr);

    // --- Инфо строка (Температура и влажность) ---
    canvas.setFont(&FreeSans9pt8b);
    canvas.setTextColor(currentTheme->textSec);
    char dataStr[30];
    snprintf(dataStr, sizeof(dataStr), "%.0fC  %.0f%%", temperature, humidity);
    
    canvas.getTextBounds(dataStr, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((160 - w) / 2, 72);
    canvas.print(dataStr);
    
    // --- Индикаторы ---
    canvas.setFont(); // Сброс для стандартных символов (если нужно)
    bool hasActiveAlarms = false;
    for(int i = 0; i < MAX_ALARMS; i++) if(alarms[i].enabled) hasActiveAlarms = true;
    
    if(hasActiveAlarms) {
        canvas.fillCircle(10, 10, 3, RGB(0, 255, 0)); // Зеленая точка - будильник
    }
    if(lampState) {
        canvas.fillCircle(150, 10, 4, currentTheme->accent);
    }

    // Popup
    if(millis() - popupTimer < 2000 && popupMsg != "") {
        canvas.fillRoundRect(20, 25, 120, 30, 5, currentTheme->accentBg);
        canvas.setTextColor(currentTheme->textMain);
        canvas.setFont(NULL); // Стандартный мелкий шрифт для попапа
        canvas.setCursor(30, 35);
        canvas.print(popupMsg);
    }
}

void drawEyes() {
    canvas.fillScreen(currentTheme->bgTop); // Просто заливка цветом темы
    uint16_t eyeColor = ST7735_WHITE, pupilColor = ST7735_BLACK;

    // Злые глаза если жарко
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
    canvas.setFont(&FreeSans9pt8b);
    canvas.setTextColor(currentTheme->textMain);
    canvas.setCursor(30,20); canvas.print("НАСТРОЙКИ");

    char buf[10];
    int y = 55;
    
    // Часы
    if (settingCursor == 0) canvas.setTextColor(currentTheme->accent); else canvas.setTextColor(currentTheme->textMain);
    canvas.setFont(&FreeSans14pt8b);
    snprintf(buf,5,"%02d",clockTime.hour);
    canvas.setCursor(40,y); canvas.print(buf);

    canvas.setTextColor(currentTheme->textMain); canvas.print(":");

    // Минуты
    if (settingCursor == 1) canvas.setTextColor(currentTheme->accent); else canvas.setTextColor(currentTheme->textMain);
    snprintf(buf,5,"%02d",clockTime.minute);
    canvas.setCursor(90,y); canvas.print(buf);
}

void drawMenu() {
    drawGradientBg();
    canvas.setFont(&FreeSans9pt8b);

    // Логика отрисовки: показываем 3 пункта, выбранный по центру
    // Но так как экран маленький, можно просто показывать один крупно или список.
    // Сделаем список с выделением.
    
    int itemHeight = 20;
    int startY = 15;
    
    // Рисуем плашку выделения
    canvas.fillRoundRect(5, startY + (menuCursor * itemHeight) - 14, 150, 18, 4, currentTheme->accentBg);

    for(int i = 0; i < MENU_ITEMS_COUNT; i++) {
        if(i == menuCursor) {
            canvas.setTextColor(currentTheme->accent);
        } else {
            canvas.setTextColor(currentTheme->textMain);
        }
        canvas.setCursor(15, startY + (i * itemHeight));
        canvas.print(menuItems[i]);
    }
    
    // Если пунктов больше чем влезает на экран, здесь нужна логика viewport'а.
    // Но сейчас 5 пунктов с высотой 20 (итого 100px) не влезут в 80px.
    // Реализуем "Окно просмотра"
}

// Улучшенная версия меню для маленького экрана
void drawMenuScrollable() {
    drawGradientBg();
    canvas.setFont(&FreeSans9pt8b);
    
    int visibleItems = 3;
    int startIdx = 0;
    
    // Вычисляем, какие элементы показывать, чтобы курсор был в центре (по возможности)
    if (menuCursor >= visibleItems) {
        startIdx = menuCursor - (visibleItems - 1);
    }
    // Коррекция, если в конце списка
    if (startIdx > MENU_ITEMS_COUNT - visibleItems) {
        startIdx = MENU_ITEMS_COUNT - visibleItems;
    }
    if (startIdx < 0) startIdx = 0;

    int y = 25; // Стартовая позиция по Y
    
    for (int i = 0; i < visibleItems; i++) {
        int itemIndex = startIdx + i;
        if (itemIndex >= MENU_ITEMS_COUNT) break;

        if (itemIndex == menuCursor) {
            // Рисуем выделение (фон)
            canvas.fillRoundRect(5, y - 16, 150, 20, 5, currentTheme->accentBg);
            canvas.setTextColor(currentTheme->accent);
            // Стрелочка слева
            canvas.fillTriangle(8, y-8, 12, y-4, 8, y, currentTheme->accent);
        } else {
            canvas.setTextColor(currentTheme->textMain);
        }
        
        canvas.setCursor(20, y);
        canvas.print(menuItems[itemIndex]);
        y += 24; // Шаг по высоте
    }
    
    // Скроллбар (опционально)
    int sbH = 80 / MENU_ITEMS_COUNT;
    canvas.fillRect(156, (80 * menuCursor) / MENU_ITEMS_COUNT, 3, sbH, currentTheme->accent);
}

void drawQuotes() {
    canvas.fillScreen(RGB(0,0,0));
    canvas.setFont(&FreeSans9pt8b);
    
    canvas.setTextColor(currentTheme->accent);
    canvas.setCursor(5,15);
    canvas.print("Цитата:");
    
    canvas.setTextColor(ST7735_WHITE);
    // Разбивка текста (простая)
    String q = String(quotes[quotePage]);
    canvas.setCursor(5, 40);
    canvas.print(q); 
}

void handleAlarmRing() {
    if ((millis()/500)%2==0) canvas.fillScreen(ST7735_RED); else canvas.fillScreen(ST7735_BLACK);
    canvas.setFont(&FreeSans14pt8b);
    canvas.setTextColor(ST7735_WHITE); 
    canvas.setCursor(10,50); canvas.print("ПОДЪЕМ!");
    if ((millis()/200)%2==0) digitalWrite(PIN_BUZZER,HIGH); else digitalWrite(PIN_BUZZER,LOW);
}

void handleDigitInput(int digit) {
    if (currentMode != MODE_SETTINGS) return;
    switch(settingCursor) {
        case 0: { int h = clockTime.hour; if(h<10) h = h*10+digit; else h=digit; if(h>23) h=digit; clockTime.hour=h; break; }
        case 1: { int m = clockTime.minute; if(m<10) m=m*10+digit; else m=digit; if(m>59) m=digit; clockTime.minute=m; break; }
    }
}

// ---------------- Обработка ИК ----------------
void handleIR() {
    if(!irrecv.decode(&results)) return;
    unsigned long key = results.value;
    
    // Глобальные кнопки
    if(key == IR_BTN_CH_MINUS) { toggleLamp(); irrecv.resume(); return; }

    // Сброс звонка
    if(currentMode == MODE_ALARM_RING) {
        currentMode = MODE_CLOCK;
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_RELAY, LOW); // Если лампа включалась будильником
        lampState = false;
        irrecv.resume();
        return;
    }

    switch(currentMode) {
        case MODE_CLOCK:
        case MODE_ANIMATION:
            if(key == IR_BTN_EQ) { currentMode = MODE_MENU; menuCursor = 0; }
            else if(key == IR_BTN_CH) currentMode = MODE_CLOCK;
            else if(key == IR_BTN_CH_PLUS) { currentMode = MODE_QUOTES; quotePage = 0; }
            else if(key == IR_BTN_PLAY) currentMode = (currentMode == MODE_CLOCK) ? MODE_ANIMATION : MODE_CLOCK;
            // Быстрые будильники
            else if(key == IR_BTN_5) { alarms[0] = {true, 5, 0}; showPopup("Аларм 5:00"); }
            else if(key == IR_BTN_7) { alarms[0] = {true, 7, 0}; showPopup("Аларм 7:00"); }
            else if(key == IR_BTN_100PLUS) { currentMode = MODE_MENU; menuCursor = 0; }
            else if(key == IR_BTN_200PLUS) resetAllAlarms();
            break;

        case MODE_SETTINGS:
            if(key == IR_BTN_EQ) { currentMode = MODE_CLOCK; showPopup("Сохранено"); }
            else if(key == IR_BTN_PREV) settingCursor = (settingCursor == 0) ? 1 : 0;
            else if(key == IR_BTN_NEXT) settingCursor = (settingCursor == 0) ? 1 : 0;
            else if(key == IR_BTN_PLUS) {
                if(settingCursor==0) clockTime.hour = (clockTime.hour + 1) % 24;
                else clockTime.minute = (clockTime.minute + 1) % 60;
            }
            else if(key == IR_BTN_MINUS) {
                if(settingCursor==0) clockTime.hour = (clockTime.hour - 1 + 24) % 24;
                else clockTime.minute = (clockTime.minute - 1 + 60) % 60;
            }
            else if(key >= IR_BTN_0 && key <= IR_BTN_9) {
                handleDigitInput((key == IR_BTN_0) ? 0 : (key - IR_BTN_1 + 1));
            }
            break;

        case MODE_MENU:
            if(key == IR_BTN_100PLUS || key == IR_BTN_EQ) {
                // Если EQ нажат в меню -> можно считать как OK или Выход
                // Давай сделаем 100+ как OK, а EQ как Выход, но здесь часто EQ это Меню
                if (key == IR_BTN_100PLUS) processMenuAction();
                else currentMode = MODE_CLOCK; // EQ - выход
            }
            // Цикличная навигация
            else if(key == IR_BTN_PREV || key == IR_BTN_MINUS) {
                menuCursor--;
                if(menuCursor < 0) menuCursor = MENU_ITEMS_COUNT - 1;
            }
            else if(key == IR_BTN_NEXT || key == IR_BTN_PLUS) {
                menuCursor++;
                if(menuCursor >= MENU_ITEMS_COUNT) menuCursor = 0;
            }
            break;

        case MODE_QUOTES:
            if(key == IR_BTN_EQ || key == IR_BTN_CH) currentMode = MODE_CLOCK;
            else if(key == IR_BTN_PREV || key == IR_BTN_MINUS) {
                quotePage--;
                if(quotePage < 0) quotePage = QUOTES_COUNT - 1;
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

    clockTime.hour=12; clockTime.minute=0; clockTime.second=0; clockTime.month=1; // Январь (Зима)
    clockTime.baseMillis=millis();
    
    updateAutoTheme(); // Применить тему сразу
}

void loop() {
    unsigned long currentMillis = millis();

    updateClock();
    updateSensors();
    handleIR();

    // Проверка будильников
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
        // Очищать экран не нужно полностью, так как мы рисуем градиент поверх всего
        // canvas.fillScreen(...) - делается внутри функций отрисовки или через drawGradientBg
        
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