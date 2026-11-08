#include "Comms.h"
#include "Config.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espClient;
static PubSubClient mqtt(espClient);
static unsigned long lastMqttRetry = 0;
static unsigned long lastTelemetryTime = 0;

// Helper: Konversi AlarmCode enum ke string untuk JSON
static const char* alarmCodeStr(AlarmCode code) {
    switch (code) {
        case ALARM_OCCLUSION: return "OCCLUSION";
        case ALARM_EMPTY:     return "EMPTY";
        default:              return "NONE";
    }
}

// Helper: Format menit sisa menjadi "HH:MM" atau "STOPPED"
static String etaFormatted(long etaMin, bool isAnomaly, bool isPaused) {
    if (isAnomaly || isPaused) return "STOPPED";
    if (etaMin <= 0) return "00:00";
    long h = etaMin / 60;
    long m = etaMin % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02ld:%02ld", h, m);
    return String(buf);
}

// Helper: Bangun JSON lengkap satu bed ke dalam JsonObject
static void buildBedJson(JsonObject& b, int i, unsigned long now) {
    long lastDropSecAgo = (long)((now - infus[i].lastDropTime) / 1000UL);

    b["bed"]             = i + 1;
    b["sensor_active"]   = (bool)infus[i].isSensorActive;
    b["paused"]          = (bool)infus[i].isPaused;
    b["anomaly"]         = (bool)infus[i].isAnomaly;
    b["alarm_code"]      = alarmCodeStr(infus[i].alarmCode);

    b["target_dpm"]      = infus[i].targetDPM;
    b["actual_dpm"]      = infus[i].actualDPM;
    b["flow_rate_ml_hr"] = serialized(String(infus[i].flowRateMlHr, 1));

    float sisaMl = (float)((int)(infus[i].sisaVolume * 10.0f)) / 10.0f;
    float volAwal = infus[i].volumeAwal;
    float pct = (volAwal > 0.0f) ? (sisaMl / volAwal * 100.0f) : 0.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    b["sisa_ml"]         = serialized(String(sisaMl, 1));
    b["volume_awal_ml"]  = serialized(String(volAwal, 1));
    b["persen_sisa"]     = serialized(String(pct, 1));
    b["eta_minutes"]     = infus[i].etaMinutes;
    b["eta_formatted"]   = etaFormatted(infus[i].etaMinutes, infus[i].isAnomaly, infus[i].isPaused);

    b["drop_factor"]     = infus[i].dropFactor;
    b["last_drop_sec_ago"] = lastDropSecAgo;
    b["servo_angle"]     = infus[i].servoAngle;
    b["pulse_count"]     = (i == 0) ? pulseCountA : pulseCountB;
}

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

                    // Hitung status sistem global
                    bool anyAnomaly = infus[0].isAnomaly || infus[1].isAnomaly;
                    const char* sysStatus = anyAnomaly ? "ALARM" : "NORMAL";

                    // === 1. Publish Bed 1 (topik khusus) ===
                    {
                        DynamicJsonDocument doc1(512);
                        doc1["device_id"]     = "MONITIS-ESP32";
                        doc1["uptime_sec"]    = now / 1000;
                        doc1["system_status"] = sysStatus;
                        JsonObject b1 = doc1.createNestedObject("bed_data");
                        buildBedJson(b1, 0, now);
                        String s1; serializeJson(doc1, s1);
                        mqtt.publish(TOPIC_BED1_TELEMETRY, s1.c_str());
                    }

                    // === 2. Publish Bed 2 (topik khusus) ===
                    {
                        DynamicJsonDocument doc2(512);
                        doc2["device_id"]     = "MONITIS-ESP32";
                        doc2["uptime_sec"]    = now / 1000;
                        doc2["system_status"] = sysStatus;
                        JsonObject b2 = doc2.createNestedObject("bed_data");
                        buildBedJson(b2, 1, now);
                        String s2; serializeJson(doc2, s2);
                        mqtt.publish(TOPIC_BED2_TELEMETRY, s2.c_str());
                    }

                    // === 3. Publish Global Summary Lengkap Medical-Grade ===
                    {
                        DynamicJsonDocument docAll(1024);
                        docAll["device_id"]     = "MONITIS-ESP32";
                        docAll["uptime_sec"]    = now / 1000;
                        docAll["wifi_rssi"]     = WiFi.RSSI();
                        docAll["ip_address"]    = WiFi.localIP().toString();
                        docAll["system_status"] = sysStatus;

                        JsonArray beds = docAll.createNestedArray("beds");
                        for (int i = 0; i < 2; i++) {
                            JsonObject b = beds.createNestedObject();
                            buildBedJson(b, i, now);
                        }
                        String sAll; serializeJson(docAll, sAll);
                        mqtt.publish(TOPIC_ALL_TELEMETRY, sAll.c_str());
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
