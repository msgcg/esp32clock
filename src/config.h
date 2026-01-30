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

// --- IR Коды (Car MP3 пульт) ---
#define IR_BTN_CH_MINUS 0xFFA25D  // CH-  вкл/выкл реле (лампа)
#define IR_BTN_CH       0xFF629D  // CH   пробуждение из анимации
#define IR_BTN_CH_PLUS  0xFFE21D  // CH+  показать рекомендации/цитаты
#define IR_BTN_PREV     0xFF22DD  // PREV листание меню
#define IR_BTN_NEXT     0xFF02FD  // NEXT листание меню
#define IR_BTN_PLAY     0xFFC23D  // PLAY/PAUSE
#define IR_BTN_MINUS    0xFFE01F  // -    уменьшить значение
#define IR_BTN_PLUS     0xFFA857  // +    увеличить значение
#define IR_BTN_EQ       0xFF906F  // EQ   открыть главное меню
#define IR_BTN_0        0xFF6897  // 0    ввод цифры
#define IR_BTN_100PLUS  0xFF9867  // 100+ сброс всех будильников
#define IR_BTN_200PLUS  0xFFB04F  // 200+ удалить ближайший будильник
#define IR_BTN_1        0xFF30CF  // 1    ввод цифры
#define IR_BTN_2        0xFF18E7  // 2    ввод цифры
#define IR_BTN_3        0xFF7A85  // 3    ввод цифры
#define IR_BTN_4        0xFF10EF  // 4    ввод цифры
#define IR_BTN_5        0xFF38C7  // 5    ввод цифры
#define IR_BTN_6        0xFF5AA5  // 6    ввод цифры
#define IR_BTN_7        0xFF42BD  // 7    ввод цифры
#define IR_BTN_8        0xFF4AB5  // 8    ввод цифры
#define IR_BTN_9        0xFF52AD  // 9    ввод цифры

#endif
