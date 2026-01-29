#ifndef CONFIG_H
#define CONFIG_H

// --- TFT (Software SPI) ---
#define TFT_MOSI    6
#define TFT_SCLK    4
#define TFT_DC      5
#define TFT_CS      0
#define TFT_RST     8

// --- Сенсоры ---
#define PIN_DHT     2
#define PIN_IR      3

// --- Актуаторы ---
#define PIN_RELAY   10
#define PIN_BUZZER  7

// --- IR Коды ---
#define IR_BTN_CH      0xFFA25D 
#define IR_BTN_CH_MIN  0xFF629D 
#define IR_BTN_CH_PLUS 0xFFE21D 
#define IR_BTN_EQ      0xFF906F 
#define IR_BTN_PREV    0xFF22DD 
#define IR_BTN_NEXT    0xFF02FD 
#define IR_BTN_PLAY    0xFFC23D 
#define IR_BTN_MINUS   0xFFE01F 
#define IR_BTN_PLUS    0xFFA857 
#define IR_BTN_0       0xFF6897
#define IR_BTN_1       0xFF30CF
#define IR_BTN_2       0xFF18E7
#define IR_BTN_3       0xFF7A85
#define IR_BTN_4       0xFF10EF
#define IR_BTN_5       0xFF38C7
#define IR_BTN_6       0xFF5AA5
#define IR_BTN_7       0xFF42BD
#define IR_BTN_8       0xFF4AB5
#define IR_BTN_9       0xFF52AD

#endif
