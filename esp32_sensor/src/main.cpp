#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"
#include "config.h"

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
bool wifiConnected = false;
unsigned long lastHeartbeat = 0;
unsigned long lastMotionTime = 0;
int motionCounter = 0;

// === ОТЛАДОЧНЫЙ РЕЖИМ ===
#define DEBUG_MODE true  // Включить подробные логи

// === ФУНКЦИИ ===
void debugPrint(String message) {
    if (DEBUG_MODE) {
        Serial.println("[DEBUG] " + message);
    }
}

void sendToServer(String eventType, String sensorId, String value = "") {
    if (!wifiConnected) {
        debugPrint("Нет WiFi, пропускаем отправку: " + eventType);
        return;
    }
    
    HTTPClient http;
    String url = "http://" + String(SERVER_IP) + "/event";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String postData = "type=" + eventType + 
                     "&sensor_id=" + sensorId + 
                     "&value=" + value;
    
    debugPrint("Отправка: " + eventType + " на " + String(SERVER_IP));
    debugPrint("Данные: " + postData);
    
    int startTime = millis();
    int httpCode = http.POST(postData);
    int endTime = millis();
    
    if (httpCode == 200) {
        debugPrint("✅ Успешно! Код: " + String(httpCode) + 
                  ", время: " + String(endTime - startTime) + "мс");
        
        // Специально для motion событий - дополнительный лог
        if (eventType == "motion") {
            Serial.println("\n🎉🎉🎉 MOTION ОТПРАВЛЕН НА СЕРВЕР! 🎉🎉🎉");
        }
    } else {
        debugPrint("❌ Ошибка! Код: " + String(httpCode));
    }
    
    http.end();
}

void checkMotionSensor() {
    static bool lastPirState = false;
    static bool motionAlreadySent = false;  // Флаг: уже отправили motion для этого срабатывания
    bool currentPirState = digitalRead(PIR_PIN);
    
    // Отладочный вывод каждые 2 секунды
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 2000) {
        Serial.print("PIR: ");
        Serial.print(currentPirState ? "HIGH" : "LOW");
        Serial.print(" | Отправлено: ");
        Serial.println(motionAlreadySent ? "ДА" : "НЕТ");
        lastDebugTime = millis();
    }
    
    // ОБНАРУЖЕНО НОВОЕ ДВИЖЕНИЕ (передний фронт) 
    // И мы еще не отправляли для этого срабатывания
    if (currentPirState == HIGH && lastPirState == LOW && !motionAlreadySent) {
        Serial.println("\n" + String('=', 40));
        Serial.println("🎯 НОВОЕ ДВИЖЕНИЕ ОБНАРУЖЕНО!");
        Serial.println(String('=', 40));
        
        // Проверяем антифлуд
        if (millis() - lastMotionTime > PIR_COOLDOWN) {
            Serial.println("🚨 ОТПРАВЛЯЮ СОБЫТИЕ MOTION!");
            
            sendToServer("motion", "pir_sensor", "detected");
            lastMotionTime = millis();
            motionAlreadySent = true; // Помечаем что отправили
            
            // Визуальная индикация
            for (int i = 0; i < 3; i++) {
                digitalWrite(STATUS_LED, LOW);
                delay(150);
                digitalWrite(STATUS_LED, HIGH);
                delay(150);
            }
        } else {
            Serial.print("⏳ Антифлуд: ждем еще ");
            Serial.print((PIR_COOLDOWN - (millis() - lastMotionTime)) / 1000);
            Serial.println(" сек");
        }
    }
    
    // ДВИЖЕНИЕ ПРЕКРАТИЛОСЬ (задний фронт)
    if (currentPirState == LOW && lastPirState == HIGH) {
        Serial.println("✅ Движение прекратилось, готов к новому срабатыванию");
        motionAlreadySent = false; // Сбрасываем флаг
    }
    lastPirState = currentPirState;
}

void setup() {
    Serial.begin(115200);
    delay(3000); // Ждем подключения монитора
    
    Serial.println("\n" + String('=', 60));
    Serial.println("       ОХРАННЫЙ ДАТЧИК - РЕЖИМ ОТЛАДКИ");
    Serial.println(String('=', 60));
    
    Serial.println("Настройки:");
    Serial.println("  PIR_PIN: " + String(PIR_PIN));
    Serial.println("  PIR_COOLDOWN: " + String(PIR_COOLDOWN) + "мс");
    Serial.println("  MOTION_SENSITIVITY: " + String(MOTION_SENSITIVITY));
    Serial.println("  SERVER_IP: " + String(SERVER_IP));
    
    // Настройка пинов
    pinMode(PIR_PIN, INPUT);
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    // Подключение к WiFi
    Serial.print("\nПодключение к WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); // Мигаем
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\n✅ WiFi подключен!");
        Serial.println("  IP: " + WiFi.localIP().toString());
        Serial.println("  RSSI: " + String(WiFi.RSSI()) + " dBm");
        digitalWrite(STATUS_LED, HIGH); // Постоянно горит
    } else {
        Serial.println("\n❌ WiFi не подключен!");
        digitalWrite(STATUS_LED, LOW);
    }
    
    // Первый heartbeat
    if (wifiConnected) {
        sendToServer("heartbeat", "sensor_init", "boot_complete");
        Serial.println("✅ Первый heartbeat отправлен");
    }
    
    // Ожидание инициализации PIR
    Serial.println("\n⏳ Ожидание инициализации PIR (60 сек)...");
    for (int i = 0; i < 60; i++) {
        delay(1000);
        Serial.print(".");
        if (i % 10 == 9) Serial.print(" ");
    }
    Serial.println("\n✅ PIR готов к работе!");
    Serial.println("Помашите рукой перед датчиком для теста");
    Serial.println(String('=', 60) + "\n");
}

void loop() {
    // Проверяем датчик движения
    checkMotionSensor();
    
    // Heartbeat каждые 30 секунд
    if (wifiConnected && millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
        sendToServer("heartbeat", "sensor_node", "alive");
        lastHeartbeat = millis();
        
        // Статус каждые 10 heartbeat
        static int heartbeatCount = 0;
        heartbeatCount++;
        if (heartbeatCount % 10 == 0) {
            Serial.println("📊 Статистика:");
            Serial.println("  Heartbeat: " + String(heartbeatCount));
            Serial.println("  Событий motion: " + String(motionCounter));
            Serial.println("  WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
        }
    }
    
    // Небольшая задержка
    delay(50);
}
