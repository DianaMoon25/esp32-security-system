#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FastBot.h>
#include "secrets.h"
#include "config.h"

WebServer server(80);
FastBot bot(BOT_TOKEN);

// Переменные состояния
bool systemArmed = false;
bool alarmActive = false;

// Вспомогательная функция для времени
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

// Обработчик POST запросов от датчиков
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

// Получение статуса
void handleStatus() {
    String status = "{\"armed\":" + String(systemArmed ? "true" : "false") + 
                   ",\"uptime\":" + String(millis() / 1000) + "}";
    server.send(200, "application/json", status);
}

// Telegram команды
void handleTelegramMessage(FB_msg& msg) {
    if (msg.text == "/arm") {
        systemArmed = true;
        bot.sendMessage("✅ Система поставлена на охрану", msg.chatID);
    } 
    else if (msg.text == "/disarm") {
        systemArmed = false;
        alarmActive = false;
        bot.sendMessage("🔓 Система снята с охраны", msg.chatID);
    }
    else if (msg.text == "/status") {
        String status = systemArmed ? "🟢 НА ОХРАНЕ" : "🔴 ВЫКЛЮЧЕНА";
        status += "\nIP: " + WiFi.localIP().toString();
        bot.sendMessage(status, msg.chatID);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2500);
    
    Serial.println("\n\n\n");
    Serial.println("═══════════════════════════════════════");
    Serial.println("   ОХРАННАЯ СИСТЕМА - ЗАПУСК");
    Serial.println("═══════════════════════════════════════");
    
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
}

void loop() {
    server.handleClient();  // Обработка HTTP-запросов
    bot.tick();             // Обработка Telegram-сообщений

    // Автоматическое отключение тревоги через 5 минут
    if (alarmActive && (millis() - alarmStartTime > ALARM_TIMEOUT)) {
        alarmActive = false;
        bot.sendMessage("⏰ Тревога автоматически отключена\nПрошло 5 минут");
        // digitalWrite(RELAY_PIN, LOW);
        Serial.println("Тревога автоматически отключена");
    }

    delay(10);
}
