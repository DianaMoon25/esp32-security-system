// rfid_tags.h - список разрешенных RFID карт
#pragma once

#include <Arduino.h>


// ===== Структура для хранения информации о карте =====
struct RFIDTag {
    String uid;        // UID карты (например: "A1 B2 C3 D4")
    String owner;      // Владелец карты
    bool active;       // Активна ли карта
};


// ===== СПИСОК РАЗРЕШЕННЫХ КАРТ =====
// ⚠️ ЗАМЕНИТЕ НА UID ВАШИХ КАРТ!
RFIDTag authorizedTags[] = {
    {"A1 B2 C3 D4", "Администратор", true},  // Ваша первая карта
    {"E5 F6 78 90", "Охранник", true},       // Ваш брелок
    {"12 34 56 78", "Гость", false}          // Отключенная карта
};


// ===== Количество карт в списке =====
const int tagCount = sizeof(authorizedTags) / sizeof(authorizedTags[0]);


// ===== Функция проверки карты =====
String checkRFIDTag(String uid) {
    for (int i = 0; i < tagCount; i++) {
        if (authorizedTags[i].uid == uid) {
            if (authorizedTags[i].active) {
                return authorizedTags[i].owner; // Возвращаем имя владельца
            } else {
                return "disabled"; // Карта отключена
            }
        }
    }
    return "unknown"; // Неизвестная карта
}
