#pragma once

// Пины
#define PIR_PIN 13
#define STATUS_LED 2

// НАСТРОЙКИ ДЛЯ РЕЖИМА L:
#define HEARTBEAT_INTERVAL 30000    // 30 секунд
#define PIR_COOLDOWN 30000          // 30 секунд (время задержки датчика ≈ 2-300 сек)
#define MOTION_SENSITIVITY 1        // 1 срабатывание