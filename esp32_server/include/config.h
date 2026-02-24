#pragma once

// Пины
#define LED_PIN 2           // Встроенный светодиод на ESP32
#define BUZZER_PIN 25


// Тайминги (в миллисекундах)
unsigned long alarmStartTime = 0;
#define BLINK_INTERVAL 1000         // для мигания LED
#define PIR_COOLDOWN 5000           // время между срабатываниями PIR
#define ALARM_TIMEOUT 300000        // таймаут тревоги

// Сообщения для Telegram
const char* ALARM_MESSAGE = "🚨 ТРЕВОГА! Обнаружено движение!";
const char* SYSTEM_ARMED_MSG = "✅ Система поставлена на охрану";
const char* SYSTEM_DISARMED_MSG = "🔓 Система снята с охраны";
