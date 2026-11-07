#include "Comms.h"
#include "Config.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqtt(espClient);
static unsigned long lastMqttRetry = 0;
static unsigned long lastTelemetryTime = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    String topicStr = String(topic);

    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, msg);
    if (!err) {
        if (xSemaphoreTake(dripMutex, (TickType_t)20)) {
            int targetBed = -1;
            if (topicStr.indexOf("bed1") >= 0) targetBed = 0;
            else if (topicStr.indexOf("bed2") >= 0) targetBed = 1;
            else if (doc.containsKey("bed")) targetBed = doc["bed"].as<int>() - 1; // bed 1 -> index 0, bed 2 -> index 1
            else if (doc.containsKey("ch")) targetBed = doc["ch"].as<int>();

            if (targetBed >= 0 && targetBed < 2) {
                if (doc.containsKey("target_dpm")) {
                    infus[targetBed].targetDPM = doc["target_dpm"];
                }
                if (doc.containsKey("pause")) {
                    infus[targetBed].isPaused = doc["pause"];
                }
                if (doc.containsKey("sensor_active")) {
                    infus[targetBed].isSensorActive = doc["sensor_active"];
                }
            }
            xSemaphoreGive(dripMutex);
        }
    }
}

void initComms() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
}

bool isMqttConnected() {
    return mqtt.connected();
}

void taskCommsCode(void *pvParameters) {
    extern void handleEncoderInput();
    extern void updateDisplay();
    unsigned long lastOledTime = 0;

    for (;;) {
        unsigned long now = millis();

        // 1. Layanan Input Rotary Encoder (Non-Blocking)
        handleEncoderInput();

        // 2. Layanan Render Display OLED tiap 100ms
        if (now - lastOledTime >= 100) {
            updateDisplay();
            lastOledTime = now;
        }

        // 3. Manajemen Jaringan WiFi & MQTT
        if (WiFi.status() == WL_CONNECTED) {
            if (!mqtt.connected()) {
                if (now - lastMqttRetry > 5000) {
                    lastMqttRetry = now;
                    String clientId = "Monitis-DualBed-" + String(random(0xffff), HEX);
                    if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
                        mqtt.subscribe(TOPIC_CONTROL_GLOBAL);
                        mqtt.subscribe(TOPIC_CONTROL_BED1);
                        mqtt.subscribe(TOPIC_CONTROL_BED2);
                    }
                }
            } else {
                mqtt.loop();

                // Publish Telemetri tiap 3 detik (Per-Bed & Global Summary)
                if (now - lastTelemetryTime >= 3000) {
                    lastTelemetryTime = now;

                    // 1. Publish Bed 1
                    DynamicJsonDocument doc1(256);
                    doc1["bed"] = 1;
                    doc1["target_dpm"] = infus[0].targetDPM;
                    doc1["actual_dpm"] = infus[0].actualDPM;
                    doc1["sisa_ml"] = infus[0].sisaVolume;
                    doc1["paused"] = infus[0].isPaused;
                    doc1["anomaly"] = infus[0].isAnomaly;
                    doc1["sensor_active"] = (bool)infus[0].isSensorActive;
                    String jsonStr1;
                    serializeJson(doc1, jsonStr1);
                    mqtt.publish(TOPIC_BED1_TELEMETRY, jsonStr1.c_str());

                    // 2. Publish Bed 2
                    DynamicJsonDocument doc2(256);
                    doc2["bed"] = 2;
                    doc2["target_dpm"] = infus[1].targetDPM;
                    doc2["actual_dpm"] = infus[1].actualDPM;
                    doc2["sisa_ml"] = infus[1].sisaVolume;
                    doc2["paused"] = infus[1].isPaused;
                    doc2["anomaly"] = infus[1].isAnomaly;
                    doc2["sensor_active"] = (bool)infus[1].isSensorActive;
                    String jsonStr2;
                    serializeJson(doc2, jsonStr2);
                    mqtt.publish(TOPIC_BED2_TELEMETRY, jsonStr2.c_str());

                    // 3. Publish Global Summary Lengkap (Semua Data Sistem & Kedua Bed Terpadu)
                    DynamicJsonDocument docAll(768);
                    docAll["device_id"] = "MONITIS-ESP32";
                    docAll["uptime_sec"] = millis() / 1000;
                    docAll["ip"] = WiFi.localIP().toString();
                    docAll["rssi"] = WiFi.RSSI();

                    JsonArray beds = docAll.createNestedArray("beds");
                    for (int i = 0; i < 2; i++) {
                        JsonObject b = beds.createNestedObject();
                        b["bed"] = i + 1;
                        b["target_dpm"] = infus[i].targetDPM;
                        b["actual_dpm"] = infus[i].actualDPM;
                        b["sisa_ml"] = (int)(infus[i].sisaVolume * 10.0f) / 10.0f;
                        b["volume_awal_ml"] = infus[i].volumeAwal;
                        float pct = (infus[i].sisaVolume / infus[i].volumeAwal) * 100.0f;
                        if (pct < 0) pct = 0;
                        if (pct > 100) pct = 100;
                        b["persen_sisa"] = (int)(pct * 10.0f) / 10.0f;
                        b["paused"] = infus[i].isPaused;
                        b["anomaly"] = infus[i].isAnomaly;
                        b["sensor_active"] = (bool)infus[i].isSensorActive;
                        b["servo_angle"] = infus[i].servoAngle;
                        b["pulse_count"] = (i == 0) ? pulseCountA : pulseCountB;
                    }
                    String jsonStrAll;
                    serializeJson(docAll, jsonStrAll);
                    mqtt.publish(TOPIC_ALL_TELEMETRY, jsonStrAll.c_str());
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
