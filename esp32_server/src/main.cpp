#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FastBot.h>
#include <vector>
#include <SPI.h>
#include <MFRC522.h>
#include "secrets.h"
#include "config.h"
#include "rfid_tags.h"

// ===== СТРУКТУРА ДЛЯ ЛОГОВ =====
struct LogEntry {
    unsigned long timestamp;  // Время в миллисекундах
    String eventType;         // Тип события: motion, arm, disarm, rfid
    String source;            // Источник: sensor1, telegram, rfid
    String details;           // Детали: "Движение в комнате", "Карта: A1 B2 C3 D4"
    bool isAlarm;             // Было ли это тревогой
};

// ===== Глобальные переменные =====
WebServer server(80);
FastBot bot(BOT_TOKEN);
bool systemArmed = false;
bool alarmActive = false;
String lastEvent = "";


// ===== RFID =====
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
unsigned long lastRFIDRead = 0;
String lastCardUID = "";
int cardReadCount = 0;


// ===== Вспомогательная функция для времени =====
String getTimeString() {
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    seconds %= 60;
    unsigned long hours = minutes / 60;
    minutes %= 60;
    String timeStr = "";
    if (hours < 10) timeStr += "0";
    timeStr += String(hours) + ":";
    if (minutes < 10) timeStr += "0";
    timeStr += String(minutes) + ":";
    if (seconds < 10) timeStr += "0";
    timeStr += String(seconds);
    return timeStr;
}


// ===== Логирование =====
std::vector<LogEntry> eventLog;
const int MAX_LOG_SIZE = 20; // Максимальное количество сохраняемых логов

// Добавление в логи
void addToLog(String type, String source, String details, bool isAlarm = false) {
    LogEntry entry;
    entry.timestamp = millis();
    entry.eventType = type;
    entry.source = source;
    entry.details = details;
    entry.isAlarm = isAlarm;
    
    // Добавляем в начало вектора (новые события сверху)
    eventLog.insert(eventLog.begin(), entry);
    
    // Ограничиваем размер
    if (eventLog.size() > MAX_LOG_SIZE) {
        eventLog.pop_back(); // Удаляем самое старое
    }
    
    // Вывод в Serial для отладки
    Serial.print("📝 Лог: [");
    Serial.print(type);
    Serial.print("] ");
    Serial.print(source);
    Serial.print(" - ");
    Serial.println(details);
}

// Получение последних n событий (n <= MAX_LOG_SIZE)
String getLastEvents(int count) {
    if (eventLog.empty()) {
        return "📭 Лог пуст";
    }
    
    String result = "📋 *Последние события*\n\n";
    result += "┌─────────────────────\n";
    
    int maxCount = min(count, (int)eventLog.size());
    for (int i = 0; i < maxCount; i++) {
        LogEntry e = eventLog[i];
        
        // Форматируем время (секунды назад)
        unsigned long secondsAgo = (millis() - e.timestamp) / 1000;
        String timeStr;
        if (secondsAgo < 60) {
            timeStr = String(secondsAgo) + " сек назад";
        } else if (secondsAgo < 3600) {
            timeStr = String(secondsAgo / 60) + " мин назад";
        } else {
            timeStr = String(secondsAgo / 3600) + " ч назад";
        }
        
        // Иконка в зависимости от типа
        String icon;
        if (e.eventType == "rfid") {
            if (e.details.indexOf("ERROR") >= 0) icon = "⛔";
            else if (e.details.indexOf("включил") >= 0) icon = "🔒";
            else if (e.details.indexOf("выключил") >= 0) icon = "🔓";
            else icon = "📇";
        }
        else if (e.eventType == "motion") icon = "👋";
        else if (e.eventType == "arm") icon = "🔒";
        else if (e.eventType == "disarm") icon = "🔓";
        else if (e.eventType == "alarm") icon = "🚨";
        else if (e.eventType == "rfid") icon = "💳";
        else if (e.eventType == "error") icon = "⚠️";
        else icon = "📌";
        
        result += "│ ";
        result += icon + " ";
        result += "[" + timeStr + "]\n";
        result += "│  " + e.source + ": " + e.details + "\n";
        
        if (i < maxCount - 1) {
            result += "├─────────────────────\n";
        }
    }
    
    result += "└─────────────────────\n";
    result += "📊 Всего событий: " + String(eventLog.size());
    
    return result;
}


// ===== Пьезо-пищалка =====
void playSound(String sound) {
    if (sound == "alarm") {
        // Прерывистый сигнал (будет обрабатываться в loop)
        alarmActive = true;
    }
    else if (sound == "boot") {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(50);
        digitalWrite(BUZZER_PIN, LOW);
        delay(50);
        digitalWrite(BUZZER_PIN, HIGH);
        delay(50);
        digitalWrite(BUZZER_PIN, LOW);
    }
    else if (sound == "rfid_ok") {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(50);
        digitalWrite(BUZZER_PIN, LOW);
    }
    else if (sound == "rfid_error") {
        for(int i = 0; i < 3; i++) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(50);
            digitalWrite(BUZZER_PIN, LOW);
            delay(50);
        }
    }
}

void handleBuzzer() {
    static unsigned long lastBuzzerToggle = 0;
    static bool buzzerState = false;
    
    if (alarmActive) {
        // Прерывистый сигнал тревоги
        if (millis() - lastBuzzerToggle > (buzzerState ? 200 : 500)) {
            buzzerState = !buzzerState;
            digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
            lastBuzzerToggle = millis();
        }
    } else {
        // Убеждаемся что сирена выключена
        digitalWrite(BUZZER_PIN, LOW);
    }
}


// ===== RFID =====
void initRFID() {
    SPI.begin();           // Инициализация SPI
    rfid.PCD_Init();       // Инициализация RFID
    rfid.PCD_DumpVersionToSerial(); // Вывод информации о модуле
    
    Serial.println("✅ RFID модуль инициализирован");
    Serial.print("Версия прошивки: 0x");
    Serial.println(rfid.PCD_ReadRegister(rfid.VersionReg), HEX);
}

void checkRFID() {
    // Проверяем с нужной периодичностью
    if (millis() - lastRFIDRead < RFID_READ_DELAY) {
        return;
    }
    lastRFIDRead = millis();
    
    // Проверяем наличие новой карты
    if (!rfid.PICC_IsNewCardPresent()) {
        return;
    }
    
    // Пытаемся прочитать карту
    if (!rfid.PICC_ReadCardSerial()) {
        return;
    }
    
    // Получаем UID карты в формате "A1 B2 C3 D4"
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) uid += "0";
        uid += String(rfid.uid.uidByte[i], HEX);
        if (i < rfid.uid.size - 1) uid += " ";
    }
    uid.toUpperCase();
    
    // Защита от повторного чтения одной карты
    if (uid == lastCardUID) {
        cardReadCount++;
        if (cardReadCount < 3) { // Пропускаем повторные чтения
            rfid.PICC_HaltA();
            return;
        }
    } else {
        lastCardUID = uid;
        cardReadCount = 1;
    }
    
    Serial.print("\n📇 RFID карта обнаружена! UID: ");
    Serial.println(uid);
    
    // Проверяем карту
    String owner = checkRFIDTag(uid);
    
    // Формируем сообщение для Telegram
    String cardMsg = "📇 RFID карта:\n";
    cardMsg += "UID: " + uid + "\n";
    
    if (owner == "unknown") {
        // Неизвестная карта
        cardMsg += "❓ Неизвестная карта!";
        bot.sendMessage(cardMsg);
        playSound("rfid_error");
        
        // Логируем попытку доступа
        addToLog("rfid", "system", "RFID_ERROR: Неизвестная карта " + uid, false);
    }
    else if (owner == "disabled") {
        // Отключенная карта
        cardMsg += "⛔ Карта отключена!";
        bot.sendMessage(cardMsg);
        playSound("rfid_error");
        
        addToLog("rfid", "system", "RFID_ERROR: Отключенная карта " + uid, false);
    }
    else {
        // Разрешенная карта - переключаем охрану
        systemArmed = !systemArmed;
        
        cardMsg += "✅ Карта: " + owner + "\n";
        cardMsg += "Действие: " + String(systemArmed ? "Система сигнализации включена" : "Система сигнализации выключена");
        
        bot.sendMessage(cardMsg);
        
        if (systemArmed) {
            playSound("arm");
            addToLog("rfid", "system", "RFID: " + owner + " включил систему сигнализации", false);
        } else {
            playSound("disarm");
            addToLog("rfid", "system", "RFID: " + owner + " выключил систему сигнализации", false);
            alarmActive = false; // Сбрасываем тревогу если была
        }
    }
    
    // Останавливаем чтение карты
    rfid.PICC_HaltA();
}


// ===== Обработчик POST запросов от датчиков =====
void handleSensorEvent() {
    Serial.println("\n═══════════════════════════════════");
    Serial.println("📥 ПОЛУЧЕН HTTP ЗАПРОС");
    Serial.print("Метод: ");
    Serial.println(server.method() == HTTP_POST ? "POST" : "GET");
    Serial.print("Клиент IP: ");
    Serial.println(server.client().remoteIP().toString());
    
    // Выводим ВСЕ аргументы
    int args = server.args();
    Serial.print("Аргументов: ");
    Serial.println(args);
    
    for (int i = 0; i < args; i++) {
        Serial.print("  ");
        Serial.print(server.argName(i));
        Serial.print(" = ");
        Serial.println(server.arg(i));
    }
    if (!server.hasArg("type") || !server.hasArg("sensor_id")) {
        server.send(400, "text/plain", "Missing parameters");
        return;
    }
    
    String eventType = server.arg("type");
    String sensorId = server.arg("sensor_id");
    String value = server.arg("value");
    
    Serial.print("📡 От датчика: ");
    Serial.print(sensorId);
    Serial.print(" - ");
    Serial.println(eventType);
    addToLog(eventType, sensorId, value, false);

    // Движение
    if (eventType == "motion") {
        String debugMsg = "🔍 Детали движения:\n";
        debugMsg += "Датчик: " + sensorId + "\n";
        debugMsg += "Значение: " + value + "\n"; 
        debugMsg += "IP источника: " + server.client().remoteIP().toString();
        bot.sendMessage(debugMsg);
        if (systemArmed && !alarmActive) {
            alarmActive = true;
            alarmStartTime = millis();

            addToLog("alarm", sensorId, "Обнаружено движение! Тревога!", true);

            playSound("alarm"); // Запускаем сирену
        
            // Отправляем в Telegram
            String alarmMsg = "🚨🚨🚨 ТРЕВОГА! 🚨🚨🚨\n";
            alarmMsg += "Обнаружено движение!\n";
            alarmMsg += "Включена звуковая сигнализация";
            
            bot.sendMessage(alarmMsg);
            
            Serial.println("🚨 АКТИВИРОВАНА ТРЕВОГА! 🚨");
        }
    }
    
    // Ответ
    String response = "{\"status\":\"ok\",\"armed\":";
    response += systemArmed ? "true" : "false";
    response += ",\"alarm\":";
    response += alarmActive ? "true" : "false";
    response += "}";
    
    server.send(200, "application/json", response);
    Serial.println("═══════════════════════════════════\n");
}


// ===== Получение статуса =====
void handleStatus() {
    String status = "{\"armed\":" + String(systemArmed ? "true" : "false") + 
                   ",\"uptime\":" + String(millis() / 1000) + "}";
    server.send(200, "application/json", status);
}


// ===== Telegram команды =====
void handleTelegramMessage(FB_msg& msg) {
    addToLog("telegram", "user", "Команда: " + msg.text, false);

    if (msg.text == "/start") {
        String welcome = "🚨 *Охранная система*\n\n";
        welcome += "Статус: " + String(systemArmed ? "🔴 НА ОХРАНЕ" : "🟢 ВЫКЛ") + "\n\n";
        welcome += "Команды:\n";
        welcome += "/status - Статус\n";
        welcome += "/arm - Включить систему сигнализации\n";
        welcome += "/disarm - Выключить систему сигнализации\n";
        welcome += "/logs - Последние 10 событий\n";
        welcome += "/clear_logs - Очистить лог\n";
        bot.sendMessage(welcome, msg.chatID);
    }

    if (msg.text == "/arm") {
        systemArmed = true;
        bot.sendMessage("✅ Система сигнализации включена", msg.chatID);
        addToLog("arm", "telegram", "Система сигнализации выключена", false);
    } 
    else if (msg.text == "/disarm") {
        systemArmed = false;
        alarmActive = false;
        bot.sendMessage("🔓 Система сигнализации выключена", msg.chatID);
        addToLog("disarm", "telegram", "Система сигнализации выключена", false);
    }
     else if (msg.text == "/logs") {
        String logs = getLastEvents(10);
        bot.sendMessage(logs, msg.chatID);
    }
    else if (msg.text == "/clear_logs") {
        eventLog.clear();
        addToLog("system", "telegram", "Лог очищен", false);
        bot.sendMessage("🧹 Лог очищен", msg.chatID);
    }
    else if (msg.text == "/test_sound") {
        playSound("boot");
        bot.sendMessage("🔊 Тест звука выполнен", msg.chatID);
    }
    else if (msg.text == "/rfid_status") {
        String rfidInfo = "📊 RFID статус:\n";
        rfidInfo += "Модуль: " + String(rfid.PCD_PerformSelfTest() ? "✅" : "❌") + "\n";
        rfidInfo += "Последняя карта: " + lastCardUID + "\n";
        rfidInfo += "Всего карт в базе: " + String(tagCount);
        bot.sendMessage(rfidInfo);
    }
    else if (msg.text == "/list_cards") {
        String list = "📋 Разрешенные карты:\n";
        for (int i = 0; i < tagCount; i++) {
            list += String(i+1) + ". " + authorizedTags[i].uid;
            list += " - " + authorizedTags[i].owner;
            list += authorizedTags[i].active ? " ✅" : " ❌";
            list += "\n";
        }
        bot.sendMessage(list);
    }
}


// ===== Стартовая настройка =====
void setup() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Выключена
    Serial.begin(115200);
    delay(2500);
    
    Serial.println("\n\n\n");
    Serial.println("═══════════════════════════════════════");
    Serial.println("   Охранная система - запуск");
    Serial.println("═══════════════════════════════════════");
    initRFID();

    Serial.println("[1] Serial инициализирован");
    
    // WiFi подключение
    Serial.print("[2] Подключаюсь к WiFi: ");
    Serial.println(WIFI_SSID);
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    Serial.print("[3] Жду подключения");
    for(int i = 0; i < 20; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        Serial.print(".");
        delay(500);
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[4] ✅ Wi-Fi подключен ");
        Serial.print("    IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[4] ❌ Wi-Fi не подключен");
        Serial.println("    Проверьте SSID/пароль в secrets.h");
    }
    
    // Настраиваем веб-сервер
    server.on("/event", HTTP_POST, handleSensorEvent);
    server.on("/status", HTTP_GET, handleStatus);
    server.begin();
    
    // Настраиваем бота
    bot.setChatID(ADMIN_CHAT_ID);
    bot.attach(handleTelegramMessage);
    bot.sendMessage("🟢 Сервер запущен. IP: " + WiFi.localIP().toString());

    Serial.println("[5] Настройка завершена");
    Serial.println("═══════════════════════════════════════\n");

    playSound("boot");
}


// ===== Основа =====
void loop() {
    server.handleClient();  // Обработка HTTP-запросов
    bot.tick();             // Обработка Telegram-сообщений
    checkRFID();          // Проверяем RFID карты
    handleBuzzer();         // Обработка звука

    // Автоматическое отключение тревоги через 5 минут
    if (alarmActive && (millis() - alarmStartTime > ALARM_TIMEOUT)) {
        alarmActive = false;
        bot.sendMessage("⏰ Тревога автоматически отключена\nПрошло 5 минут");
        Serial.println("Тревога автоматически отключена");
    }

    // Проверка Wi-Fi
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 30000) { // Каждые 30 секунд
            Serial.println("🔄 Потеря WiFi, переподключение...");
            WiFi.reconnect();
            lastReconnect = millis();
        }

    delay(10);
}
