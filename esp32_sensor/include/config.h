#pragma once

// Пины для ESP32-S3
#define PIR_PIN 18              // Датчик движения HC-SR501
#define STATUS_LED 48           // Встроенный светодиод на ESP32-S3 (обычно GPIO48)
#define BUTTON_PIN 0            // Кнопка BOOT на S3 (GPIO0)

// Настройки для ESP32-S3
#define HEARTBEAT_INTERVAL 30000    // 30 секунд
#define PIR_COOLDOWN 10000          // 10 секунд антифлуд
#define MOTION_SENSITIVITY 1        // 1 срабатывание = отправка

// Режим USB CDC (для Serial через USB)
#define USE_USB_CDC true            // true = использовать USB для Serial
