#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FastBot.h>
#include <SPI.h>
#include <MFRC522.h>
#include "secrets.h"
#include "config.h"
#include "rfid_tags.h"

WebServer server(80);
FastBot bot(BOT_TOKEN);


// ===== RFID =====
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
unsigned long lastRFIDRead = 0;
String lastCardUID = "";
int cardReadCount = 0;


// ===== Переменные состояния =====
bool systemArmed = false;
bool alarmActive = false;
unsigned long alarmStartTime = 0;
String lastEvent = "";


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
void logEvent(String event) {
    lastEvent = event;
    // Здесь сохраняем в файл/EEPROM
    Serial.println("Событие: " + event);
    // Отправляем всем админам (можно хранить chatID в EEPROM)
    bot.sendMessage("📝 " + event, "ADMIN_CHAT_ID");
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


// === ФУНКЦИЯ ДЛЯ RFID ===
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
        cardMsg += "⛔ НЕИЗВЕСТНАЯ КАРТА!";
        bot.sendMessage(cardMsg);
        playSound("rfid_error");
        
        // Логируем попытку доступа
        logEvent("RFID_ERROR: Неизвестная карта " + uid);
    }
    else if (owner == "disabled") {
        // Отключенная карта
        cardMsg += "⛔ КАРТА ОТКЛЮЧЕНА!";
        bot.sendMessage(cardMsg);
        playSound("rfid_error");
        
        logEvent("RFID_ERROR: Отключенная карта " + uid);
    }
    else {
        // Разрешенная карта - переключаем охрану
        systemArmed = !systemArmed;
        
        cardMsg += "✅ Карта: " + owner + "\n";
        cardMsg += "Действие: " + String(systemArmed ? "ОХРАНА ВКЛ" : "ОХРАНА ВЫКЛ");
        
        bot.sendMessage(cardMsg);
        
        if (systemArmed) {
            playSound("arm");
            logEvent("RFID: " + owner + " включил охрану");
        } else {
            playSound("disarm");
            logEvent("RFID: " + owner + " выключил охрану");
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
    
    // Heartbeat
    if (eventType == "heartbeat") {
        static unsigned long lastHeartbeatNotify = 0;
        if (millis() - lastHeartbeatNotify > 120000) { // Раз в 2 минуты
            String msg = "📡 Датчик онлайн\n";
            msg += "IP: " + server.client().remoteIP().toString() + "\n";
            msg += "Сигнал WiFi: " + String(WiFi.RSSI()) + " dBm";
            bot.sendMessage(msg);
            lastHeartbeatNotify = millis();
        }
    }

    // Движение
    else if (eventType == "motion") {
        String debugMsg = "🔍 Детали движения:\n";
        debugMsg += "Датчик: " + sensorId + "\n";
        debugMsg += "Значение: " + value + "\n"; 
        debugMsg += "IP источника: " + server.client().remoteIP().toString();
        bot.sendMessage(debugMsg);
        if (systemArmed && !alarmActive) {
            alarmActive = true;
            alarmStartTime = millis();

            playSound("alarm"); // Запускаем сирену
        
            // Отправляем в Telegram
            String alarmMsg = "🚨🚨🚨 ТРЕВОГА! 🚨🚨🚨\n";
            alarmMsg += "Обнаружено движение!\n";
            alarmMsg += "Включена звуковая сигнализация";
            
            bot.sendMessage(alarmMsg);
            
            Serial.println("🚨 АКТИВИРОВАНА ТРЕВОГА!");
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
    if (msg.text == "/arm") {
        systemArmed = true;
        bot.sendMessage("✅ Система поставлена на охрану", msg.chatID);
        logEvent("Система активирована через Telegram");
    } 
    else if (msg.text == "/disarm") {
        systemArmed = false;
        alarmActive = false;
        bot.sendMessage("🔓 Система снята с охраны", msg.chatID);
        logEvent("Система деактивирована через Telegram");
    }
    else if (msg.text == "/status") {
        String status = systemArmed ? "🟢 НА ОХРАНЕ" : "🔴 ВЫКЛЮЧЕНА";
        status += "\nIP: " + WiFi.localIP().toString();
        bot.sendMessage(status, msg.chatID);
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
    
    else if (msg.text.startsWith("/add_card")) {
        // Команда для добавления карты: /add_card Имя
        String owner = msg.text.substring(9);
        if (owner.length() > 0) {
            // Просим приложить карту
            bot.sendMessage("📌 Приложите карту для добавления как '" + owner + "'");
            
            // Здесь нужно реализовать режим обучения
            // Пока просто заглушка
            bot.sendMessage("⚠️ Функция в разработке");
        }
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
    Serial.println("   ОХРАННАЯ СИСТЕМА - ЗАПУСК");
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
        Serial.println("[4] ✅ WiFi ПОДКЛЮЧЕН!");
        Serial.print("    IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[4] ❌ WiFi НЕ ПОДКЛЮЧЕН!");
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
    logEvent("Система загружена");
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

    delay(10);
}
