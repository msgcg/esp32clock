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
enum AppMode {MODE_CLOCK, MODE_ANIMATION, MODE_SETTINGS, MODE_ALARM_RING};
AppMode currentMode = MODE_CLOCK;

bool lampState = false;
float temperature = 0;
float humidity = 0;

struct Alarm { bool enabled; uint8_t hour; uint8_t minute; } myAlarm = {false, 7, 0};
int settingCursor = 0;

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
    myAlarm.hour = h;
    myAlarm.minute = m;
    myAlarm.enabled = true;
    showPopup("Alarm: " + String(h) + ":00");
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

    if (myAlarm.enabled) { canvas.setTextColor(ST7735_GREEN); canvas.setCursor(5,5); canvas.print("ALM"); }
    if (lampState) canvas.fillCircle(150,10,4,ST7735_YELLOW);

    if (millis() - popupTimer < 2000 && popupMsg != "") {
        canvas.fillRoundRect(30, 25, 100, 30, 5, ST7735_BLUE);
        canvas.setTextColor(ST7735_WHITE);
        canvas.setCursor(40,35);
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
    if(irrecv.decode(&results)) {
        unsigned long key = results.value;
        if(key == IR_BTN_CH) toggleLamp();
        if(currentMode == MODE_ALARM_RING) { currentMode = MODE_CLOCK; myAlarm.enabled=false; digitalWrite(PIN_BUZZER,LOW); digitalWrite(PIN_RELAY,LOW); irrecv.resume(); return; }

        switch(currentMode) {
            case MODE_CLOCK:
            case MODE_ANIMATION:
                if(key==IR_BTN_EQ){ currentMode=MODE_SETTINGS; settingCursor=0; }
                else if(key==IR_BTN_CH_PLUS || key==IR_BTN_CH_MIN){ currentMode=(currentMode==MODE_CLOCK)?MODE_ANIMATION:MODE_CLOCK; }
                else if(key==IR_BTN_1) setAlarmShortcut(7,0);
                else if(key==IR_BTN_2) setAlarmShortcut(8,0);
                else if(key==IR_BTN_3) setAlarmShortcut(9,0);
                else if(key==IR_BTN_0){ myAlarm.enabled=false; showPopup("Alarm OFF"); }
                break;

            case MODE_SETTINGS:
                if(key==IR_BTN_EQ){ currentMode=MODE_CLOCK; showPopup("Saved"); }
                else if(key==IR_BTN_PREV){ settingCursor--; if(settingCursor<0) settingCursor=1; }
                else if(key==IR_BTN_NEXT){ settingCursor++; if(settingCursor>1) settingCursor=0; }
                else if(key>=IR_BTN_0 && key<=IR_BTN_9) handleDigitInput(key - IR_BTN_0);
                break;
        }
        irrecv.resume(); delay(150);
    }
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

    // Будильник
    if(myAlarm.enabled && clockTime.hour==myAlarm.hour && clockTime.minute==myAlarm.minute && clockTime.second==0){
        currentMode=MODE_ALARM_RING;
        lampState=true;
        digitalWrite(PIN_RELAY,HIGH);
    }

    if(currentMillis - lastDraw > 50){
        switch(currentMode){
            case MODE_CLOCK: drawClock(); break;
            case MODE_ANIMATION: drawEyes(); break;
            case MODE_SETTINGS: drawSettings(); break;
            case MODE_ALARM_RING: handleAlarmRing(); break;
        }
        tft.drawRGBBitmap(0,0,canvas.getBuffer(),160,80);
        lastDraw=currentMillis;
    }
}
