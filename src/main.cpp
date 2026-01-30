#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <DHT.h>
#include "config.h"

// ---------------- Объекты ----------------
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
IRrecv irrecv(PIN_IR);
decode_results results;
DHT dht(PIN_DHT, DHT11);

// ---------------- Глобальные переменные ----------------
enum AppMode {MODE_CLOCK, MODE_ANIMATION, MODE_SETTINGS, MODE_ALARM_RING, MODE_MENU, MODE_QUOTES};
AppMode currentMode = MODE_CLOCK;

bool lampState = false;
float temperature = 0;
float humidity = 0;

#define MAX_ALARMS 5
struct Alarm { bool enabled; uint8_t hour; uint8_t minute; };
Alarm alarms[MAX_ALARMS] = {{false, 7, 0}, {false, 8, 0}, {false, 9, 0}, {false, 0, 0}, {false, 0, 0}};
int alarmCount = 1;

int settingCursor = 0;
int menuCursor = 0;  // 0=Будильники, 1=Свет, 2=Эффекты, 3=Рекомендации, 4=Звук
int quotePage = 0;

// Soft Clock
struct SoftClock {
    int hour;
    int minute;
    int second;
    unsigned long baseMillis;
} clockTime;

// Холст
GFXcanvas16 canvas(160, 80);

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

// Рекомендации/цитаты
const char* quotes[] = {
    "Be happy!",
    "Good morning!",
    "Have a nice day!",
    "Keep smiling!",
    "You are awesome!"
};
#define QUOTES_COUNT 5

// ---------------- Вспомогательные функции ----------------
void toggleLamp() {
    lampState = !lampState;
    digitalWrite(PIN_RELAY, lampState ? HIGH : LOW);
}

void showPopup(String msg) {
    popupMsg = msg;
    popupTimer = millis();
}

void setAlarmShortcut(int h, int m) {
    alarms[0].hour = h;
    alarms[0].minute = m;
    alarms[0].enabled = true;
    showPopup("Alarm: " + String(h) + ":00");
}

void resetAllAlarms() {
    for(int i = 0; i < MAX_ALARMS; i++) {
        alarms[i].enabled = false;
    }
    alarmCount = 0;
    showPopup("All alarms OFF");
}

void processMenuAction() {
    // Выполнить действие выбранного пункта меню (срабатывает при нажатии 100+)
    switch(menuCursor) {
        case 0: // Alarms
            currentMode = MODE_SETTINGS;
            settingCursor = 0;
            showPopup("Set alarm time");
            break;
        case 1: // Light
            toggleLamp();
            showPopup(lampState ? "Light ON" : "Light OFF");
            break;
        case 2: // Effects
            currentMode = MODE_ANIMATION;
            showPopup("Animation ON");
            break;
        case 3: // Quotes
            currentMode = MODE_QUOTES;
            quotePage = 0;
            break;
        case 4: // Sound
            showPopup("Sound menu");
            break;
    }
    currentMode = MODE_CLOCK;
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
}

// ---------------- Отрисовка ----------------
void drawWeatherEffects() {
    if (humidity > 70) { // Дождь
        for (int i = 0; i < 5; i++) canvas.drawFastVLine(random(0,160), random(0,80), 3, ST7735_BLUE);
    }
    if (temperature < 0) { // Снег
        for (int i = 0; i < 5; i++) canvas.drawPixel(random(0,160), random(0,80), ST7735_WHITE);
    }
}

void drawClock() {
    canvas.fillScreen(ST7735_BLACK);
    drawWeatherEffects();

    canvas.setTextColor(ST7735_WHITE);
    canvas.setTextSize(3);
    canvas.setCursor(35, 25);
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", clockTime.hour, clockTime.minute);
    canvas.print(timeStr);

    canvas.setTextSize(1);
    canvas.setCursor(25, 60);
    char dataStr[30];
    snprintf(dataStr, sizeof(dataStr), "T:%.0f H:%.0f%%", temperature, humidity);
    canvas.print(dataStr);

    // Индикаторы состояния
    bool hasActiveAlarms = false;
    for(int i = 0; i < MAX_ALARMS; i++) {
        if(alarms[i].enabled) {
            hasActiveAlarms = true;
            break;
        }
    }
    
    if(hasActiveAlarms) {
        canvas.setTextColor(ST7735_GREEN);
        canvas.setCursor(5, 5);
        canvas.print("ALM");
    }
    if(lampState) canvas.fillCircle(150, 10, 4, ST7735_YELLOW);

    if(millis() - popupTimer < 2000 && popupMsg != "") {
        canvas.fillRoundRect(30, 25, 100, 30, 5, ST7735_BLUE);
        canvas.setTextColor(ST7735_WHITE);
        canvas.setCursor(40, 35);
        canvas.print(popupMsg);
    }
}

void drawEyes() {
    canvas.fillScreen(ST7735_BLACK);
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
    canvas.fillScreen(ST7735_BLUE);
    canvas.setTextColor(ST7735_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(5,5); canvas.print("SETTINGS");

    char buf[10];
    int y = 30, x = 20;
    canvas.setTextSize(2);

    if (settingCursor == 0) canvas.setTextColor(ST7735_YELLOW); else canvas.setTextColor(ST7735_WHITE);
    snprintf(buf,5,"%02d",clockTime.hour);
    canvas.setCursor(x,y); canvas.print(buf); x+=25;

    canvas.setTextColor(ST7735_WHITE); canvas.print(":"); x+=10;

    if (settingCursor == 1) canvas.setTextColor(ST7735_YELLOW); else canvas.setTextColor(ST7735_WHITE);
    snprintf(buf,5,"%02d",clockTime.minute);
    canvas.setCursor(x,y); canvas.print(buf);
}

void drawMenu() {
    canvas.fillScreen(ST7735_BLACK);
    canvas.setTextColor(ST7735_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(5,5);
    
    const char* menuItems[] = {"Alarms", "Light", "Effects", "Quotes", "Sound"};
    canvas.print("MENU");
    
    int y = 25;
    for(int i = 0; i < 5; i++) {
        if(i == menuCursor) {
            canvas.setTextColor(ST7735_YELLOW);
            canvas.fillRoundRect(5, y-2, 150, 12, 2, ST7735_BLUE);
            canvas.setTextColor(ST7735_BLACK);
        } else {
            canvas.setTextColor(ST7735_WHITE);
        }
        canvas.setCursor(10, y);
        canvas.print(menuItems[i]);
        y += 12;
    }
}

void drawQuotes() {
    canvas.fillScreen(ST7735_BLACK);
    canvas.setTextColor(ST7735_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(10,10);
    canvas.println("QUOTES");
    
    canvas.setTextColor(ST7735_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(5, 30);
    
    int quoteIdx = quotePage % QUOTES_COUNT;
    String quote = String(quotes[quoteIdx]);
    
    // Простой перенос текста
    if(quote.length() > 20) {
        int i = 0;
        int line = 30;
        while(i < quote.length() && line < 70) {
            canvas.setCursor(5, line);
            canvas.println(quote.substring(i, i+20));
            i += 20;
            line += 12;
        }
    } else {
        canvas.setCursor(5, 40);
        canvas.println(quote);
    }
}

void handleAlarmRing() {
    if ((millis()/500)%2==0) canvas.fillScreen(ST7735_RED); else canvas.fillScreen(ST7735_BLACK);
    canvas.setTextColor(ST7735_WHITE); canvas.setTextSize(2); canvas.setCursor(30,35); canvas.print("WAKE UP!");
    if ((millis()/200)%2==0) digitalWrite(PIN_BUZZER,HIGH); else digitalWrite(PIN_BUZZER,LOW);
}

void handleDigitInput(int digit) {
    if (currentMode != MODE_SETTINGS) return;
    switch(settingCursor) {
        case 0: { int h = clockTime.hour; if(h<10) h = h*10+digit; else h=digit; if(h>23) h=digit; clockTime.hour=h; break; }
        case 1: { int m = clockTime.minute; if(m<10) m=m*10+digit; else m=digit; if(m>59) m=digit; clockTime.minute=m; break; }
    }
}

void handleIR() {
    if(!irrecv.decode(&results)) return;
    
    unsigned long key = results.value;
    
    // CH- всегда вкл/выкл реле (лампа)
    if(key == IR_BTN_CH_MINUS) {
        toggleLamp();
        irrecv.resume();
        return;
    }
    
    // Отключить будильник при звонке
    if(currentMode == MODE_ALARM_RING) {
        currentMode = MODE_CLOCK;
        for(int i = 0; i < MAX_ALARMS; i++) alarms[i].enabled = false;
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_RELAY, LOW);
        lampState = false;
        irrecv.resume();
        return;
    }

    switch(currentMode) {
        case MODE_CLOCK:
        case MODE_ANIMATION:
            if(key == IR_BTN_EQ) {
                currentMode = MODE_MENU;
                menuCursor = 0;
            }
            else if(key == IR_BTN_CH) {
                // Пробуждение из анимации
                currentMode = MODE_CLOCK;
            }
            else if(key == IR_BTN_CH_PLUS) {
                // Показать рекомендации/цитаты
                currentMode = MODE_QUOTES;
                quotePage = 0;
            }
            else if(key == IR_BTN_PLAY) {
                // Переключить между часами и анимацией
                currentMode = (currentMode == MODE_CLOCK) ? MODE_ANIMATION : MODE_CLOCK;
            }
            // Быстрая установка будильников на 5, 7, 9 утра
            else if(key == IR_BTN_5) {
                setAlarmShortcut(5, 0);
            }
            else if(key == IR_BTN_7) {
                setAlarmShortcut(7, 0);
            }
            else if(key == IR_BTN_9) {
                setAlarmShortcut(9, 0);
            }
            else if(key == IR_BTN_100PLUS) {
                // 100+ - открыть меню
                currentMode = MODE_MENU;
                menuCursor = 0;
            }
            else if(key == IR_BTN_200PLUS) {
                // 200+ - сброс всех будильников
                resetAllAlarms();
            }
            break;

        case MODE_SETTINGS:
            if(key == IR_BTN_EQ) {
                currentMode = MODE_CLOCK;
                showPopup("Saved");
            }
            else if(key == IR_BTN_PREV) {
                settingCursor--;
                if(settingCursor < 0) settingCursor = 1;
            }
            else if(key == IR_BTN_NEXT) {
                settingCursor++;
                if(settingCursor > 1) settingCursor = 0;
            }
            else if(key == IR_BTN_PLUS) {
                if(settingCursor == 0) {
                    clockTime.hour++;
                    if(clockTime.hour > 23) clockTime.hour = 0;
                } else {
                    clockTime.minute++;
                    if(clockTime.minute > 59) clockTime.minute = 0;
                }
            }
            else if(key == IR_BTN_MINUS) {
                if(settingCursor == 0) {
                    clockTime.hour--;
                    if(clockTime.hour < 0) clockTime.hour = 23;
                } else {
                    clockTime.minute--;
                    if(clockTime.minute < 0) clockTime.minute = 59;
                }
            }
            else if(key >= IR_BTN_0 && key <= IR_BTN_9) {
                handleDigitInput((key == IR_BTN_0) ? 0 : (key - IR_BTN_1 + 1));
            }
            break;

        case MODE_MENU:
            if(key == IR_BTN_100PLUS) {
                // 100+ - OK (выполнить действие меню)
                processMenuAction();
            }
            else if(key == IR_BTN_EQ) {
                // EQ - закрыть меню
                currentMode = MODE_CLOCK;
            }
            else if(key == IR_BTN_PREV || key == IR_BTN_MINUS) {
                menuCursor--;
                if(menuCursor < 0) menuCursor = 4;
            }
            else if(key == IR_BTN_NEXT || key == IR_BTN_PLUS) {
                menuCursor++;
                if(menuCursor > 4) menuCursor = 0;
            }
            break;

        case MODE_QUOTES:
            if(key == IR_BTN_EQ || key == IR_BTN_CH) {
                currentMode = MODE_CLOCK;
            }
            else if(key == IR_BTN_PREV || key == IR_BTN_MINUS) {
                quotePage--;
                if(quotePage < 0) quotePage = QUOTES_COUNT - 1;
            }
            else if(key == IR_BTN_NEXT || key == IR_BTN_PLUS) {
                quotePage++;
            }
            break;

        default:
            break;
    }

    irrecv.resume();
    delay(150);
}

// ---------------- setup ----------------
void setup() {
    pinMode(PIN_RELAY,OUTPUT); digitalWrite(PIN_RELAY,LOW);
    pinMode(PIN_BUZZER,OUTPUT); digitalWrite(PIN_BUZZER,LOW);

    dht.begin();
    irrecv.enableIRIn();

    tft.initR(INITR_MINI160x80); tft.setRotation(3); tft.fillScreen(ST7735_BLACK);

    clockTime.hour=7; clockTime.minute=0; clockTime.second=0; clockTime.baseMillis=millis();
}

// ---------------- loop ----------------
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
        switch(currentMode) {
            case MODE_CLOCK: drawClock(); break;
            case MODE_ANIMATION: drawEyes(); break;
            case MODE_SETTINGS: drawSettings(); break;
            case MODE_MENU: drawMenu(); break;
            case MODE_QUOTES: drawQuotes(); break;
            case MODE_ALARM_RING: handleAlarmRing(); break;
        }
        tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 80);
        lastDraw = currentMillis;
    }
}
