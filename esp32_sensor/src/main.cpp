#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <WebServer.h>
#include <HTTPClient.h>
#include "secrets.h"
#include "config.h"

// ===== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =====
bool wifiConnected = false;
unsigned long lastMotionTime = 0;
int motionCounter = 0;
bool motionAlreadySent = false;  // Флаг для режима La

#define DEBUG_MODE true


// ===== ФУНКЦИИ =====
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
    
    Serial.print("📤 Отправка: ");
    Serial.print(eventType);
    Serial.print(" с value=");
    Serial.println(value);
    
    int httpCode = http.POST(postData);
    
    if (httpCode == 200) {
        debugPrint("✅ Успешно! Код: " + String(httpCode));
        
        // Для motion событий - визуальная индикация
        if (eventType == "motion") {
            Serial.println("\n🎯 MOTION ОТПРАВЛЕН НА СЕРВЕР!");
        }
    } else {
        debugPrint("❌ Ошибка! Код: " + String(httpCode));
    }
    
    http.end();
}

void checkMotionSensor() {
    static bool lastPirState = false;
    static bool motionActive = false;
    bool currentPirState = digitalRead(PIR_PIN);
    
    // НОВОЕ ДВИЖЕНИЕ (LOW -> HIGH)
    if (currentPirState == HIGH && lastPirState == LOW) {
        Serial.println("\n🔴 ДВИЖЕНИЕ ОБНАРУЖЕНО!");
        motionActive = true;
        
        // Проверяем антифлуд
        if (millis() - lastMotionTime > PIR_COOLDOWN && !motionAlreadySent) {
            Serial.println("📤 ОТПРАВКА НА СЕРВЕР!");
            
            sendToServer("motion", "pir_sensor", "detected");
            lastMotionTime = millis();
            motionAlreadySent = true;
            
            // Мигаем светодиодом
            for (int i = 0; i < 5; i++) {
                digitalWrite(STATUS_LED, HIGH);
                delay(100);
                digitalWrite(STATUS_LED, LOW);
                delay(100);
            }
        }
    }
    
    // ДВИЖЕНИЕ ПРЕКРАТИЛОСЬ (HIGH -> LOW)
    if (currentPirState == LOW && lastPirState == HIGH) {
        Serial.println("🟢 Движение прекратилось");
        motionActive = false;
        motionAlreadySent = false;  // Сбрасываем флаг для следующего срабатывания
    }
    
    lastPirState = currentPirState;
}

void setup() {
    Serial.begin(115200);
    delay(3000);
    
    Serial.println("\n" + String('=', 60));
    Serial.println("    ОХРАННЫЙ ДАТЧИК НА ESP32-S3");
    Serial.println(String('=', 60));
    
    Serial.print("Чип: ");
    Serial.println(ESP.getChipModel());
    Serial.print("Тактовая частота: ");
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(" MHz");
    
    // Настройка пинов
    pinMode(PIR_PIN, INPUT);
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    Serial.println("\n📡 Настройки:");
    Serial.println("  PIR_PIN: GPIO" + String(PIR_PIN));
    Serial.println("  STATUS_LED: GPIO" + String(STATUS_LED));
    Serial.println("  PIR_COOLDOWN: " + String(PIR_COOLDOWN) + " мс");
    Serial.println("  SERVER_IP: " + String(SERVER_IP));
    
    // Подключение к WiFi
    Serial.print("\n📶 Подключение к WiFi: ");
    Serial.println(WIFI_SSID);
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\n✅ WiFi подключен!");
        Serial.println("  IP адрес: " + WiFi.localIP().toString());
        Serial.println("  MAC адрес: " + WiFi.macAddress());
        Serial.println("  RSSI: " + String(WiFi.RSSI()) + " dBm");
        digitalWrite(STATUS_LED, HIGH); // Постоянно горит
    } else {
        Serial.println("\n❌ Ошибка подключения к WiFi!");
        // Режим аварийной индикации
        while (true) {
            digitalWrite(STATUS_LED, HIGH);
            delay(200);
            digitalWrite(STATUS_LED, LOW);
            delay(200);
        }
    }
    
    // Инициализация PIR (ждем 30 секунд)
    Serial.println("\n⏳ Инициализация PIR датчика (30 сек)...");
    for (int i = 0; i < 30; i++) {
        delay(1000);
        Serial.print(".");
        if (i % 10 == 9) Serial.print(" ");
    }
    
    Serial.println("\n✅ Система готова к работе!");
    Serial.println(String('=', 60) + "\n");
    
    // Короткий звуковой сигнал готовности
    for (int i = 0; i < 2; i++) {
        digitalWrite(STATUS_LED, HIGH);
        delay(100);
        digitalWrite(STATUS_LED, LOW);
        delay(100);
    }
}

void loop() {
    // Проверка датчика движения
    checkMotionSensor();
    
    // Проверка WiFi соединения
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        digitalWrite(STATUS_LED, LOW);
        
        // Пытаемся переподключиться
        static unsigned long lastReconnect = 0;
        if (millis() - lastReconnect > 30000) { // Каждые 30 секунд
            Serial.println("🔄 Потеря WiFi, переподключение...");
            WiFi.reconnect();
            lastReconnect = millis();
        }
    } else {
        wifiConnected = true;
        digitalWrite(STATUS_LED, HIGH);
    }
    
    delay(100);
}
