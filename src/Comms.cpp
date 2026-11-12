#include "Comms.h"
#include "Config.h"
#include "Control.h"
#include "PumpOutput.h"
#include "Sensor.h"
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
        case ALARM_FLOW_NOT_DETECTED: return "FLOW_NOT_DETECTED";
        case ALARM_PUMP_OUTPUT_FAILURE: return "PUMP_OUTPUT_FAILURE";
        default:              return "NONE";
    }
}

// Helper: Format menit sisa menjadi "HH:MM" atau "STOPPED"
static String etaFormatted(long etaMin, BedState state) {
    if (state != BED_RUNNING) return "STOPPED";
    if (etaMin <= 0) return "00:00";
    long h = etaMin / 60;
    long m = etaMin % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02ld:%02ld", h, m);
    return String(buf);
}

// Helper: Bangun JSON lengkap satu bed ke dalam JsonObject
static bool buildBedJson(JsonObject& b, int i, unsigned long now) {
    if (!xSemaphoreTake(dripMutex, pdMS_TO_TICKS(20)))
        return false;

    long lastDropSecAgo = (long)((now - getLastDropTime_Safe(i)) / 1000UL);

    b["bed"]             = i + 1;
    b["state"]           = bedStateName(infus[i].state);
    b["configuration_confirmed"] = (bool)infus[i].configurationConfirmed;
    b["sensor_active"]   = (bool)infus[i].isSensorActive;
    b["paused"]          = infus[i].state == BED_PAUSED;
    b["anomaly"]         = infus[i].state == BED_ALARM;
    b["alarm_active"]    = infus[i].state == BED_ALARM;
    b["alarm_acknowledged"] = (bool)infus[i].alarmAcknowledged;
    b["alarm_recovery_ready"] = (bool)infus[i].alarmRecoveryReady;
    b["alarm_code"]      = alarmCodeStr(infus[i].alarmCode);

    b["target_dpm"]      = infus[i].targetDPM;
    b["actual_dpm"]      = infus[i].actualDPM;
    b["actual_dpm_valid"] = (bool)infus[i].dpmValid;
    b["actual_dpm_stale"] = (bool)infus[i].dpmStale;
    b["measurement_status"] = !infus[i].isSensorActive
                                  ? "SENSOR_DISABLED"
                                  : infus[i].dpmValid
                                        ? "VALID"
                                        : infus[i].dpmStale
                                              ? "STALE"
                                              : infus[i].state == BED_ALARM
                                                    ? infus[i].alarmCode ==
                                                              ALARM_FLOW_NOT_DETECTED
                                                          ? "NO_DROPS_DETECTED"
                                                          : "OUTPUT_FAULT"
                                                    : infus[i].state ==
                                                              BED_RUNNING
                                                          ? "MEASURING"
                                                          : infus[i].state ==
                                                                    BED_PAUSED
                                                                ? "PAUSED"
                                                                : "STANDBY";
    if (infus[i].dropFactor > 0) {
        b["target_flow_ml_hr"] =
            serialized(String(infus[i].targetFlowRateMlHr, 1));
        if (infus[i].dpmValid)
            b["estimated_flow_ml_hr"] =
                serialized(String(infus[i].measuredFlowRateMlHr, 1));
        else
            b["estimated_flow_ml_hr"] = nullptr;
    } else {
        b["target_flow_ml_hr"] = nullptr;
        b["estimated_flow_ml_hr"] = nullptr;
    }
    b["flow_measurement_source"] = "drop_sensor_nominal_conversion";
    b["flow_rate_is_direct_measurement"] = false;

    float sisaMl = (float)((int)(infus[i].sisaVolume * 10.0f)) / 10.0f;
    float volAwal = infus[i].volumeAwal;
    float pct = (volAwal > 0.0f) ? (sisaMl / volAwal * 100.0f) : 0.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    b["sisa_ml"]         = serialized(String(sisaMl, 1));
    b["volume_awal_ml"]  = serialized(String(volAwal, 1));
    b["persen_sisa"]     = serialized(String(pct, 1));
    b["eta_minutes"]     = infus[i].etaMinutes;
    b["eta_formatted"]   = etaFormatted(infus[i].etaMinutes, infus[i].state);

    b["drop_factor"]     = infus[i].dropFactor;
    b["last_drop_sec_ago"] = lastDropSecAgo;
    const bool pumpReady = isPumpOutputReady(i);
    b["pump_output_enabled"] = (bool)PUMP_OUTPUT_ENABLED;
    b["pump_output_ready"] = pumpReady;
    b["pump_status"] =
        infus[i].alarmCode == ALARM_PUMP_OUTPUT_FAILURE
            ? "OUTPUT_DISABLE_OR_COMMAND_FAILED"
            : infus[i].pumpCommandActive
                  ? "FLOW_REQUEST_ACTIVE"
                  : pumpReady ? "READY_IDLE"
                              : "DISABLED_UNCONFIGURED";
    b["pump_command_known"] = (bool)infus[i].pumpCommandKnown;
    b["pump_command_active"] = (bool)infus[i].pumpCommandActive;
    if (infus[i].pumpCommandKnown)
        b["pump_command_flow_ml_hr"] =
            serialized(String(infus[i].pumpCommandFlowRateMlHr, 1));
    else
        b["pump_command_flow_ml_hr"] = nullptr;
    b["pulse_count"]     = getPulseCount_Safe(i);
    xSemaphoreGive(dripMutex);
    return true;
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
                DripChannel &bed = infus[targetBed];
                if (doc.containsKey("target_dpm") && bed.state == BED_STANDBY) {
                    int target = doc["target_dpm"].as<int>();
                    if (target >= 5 && target <= 150) {
                        bed.targetDPM = target;
                        bed.configurationConfirmed = false;
                    }
                }
                if (doc["pause"] | false) {
                    if (bed.state == BED_RUNNING &&
                        preparePumpOutputForInactiveState(targetBed))
                        bed.state = BED_PAUSED;
                }
                if (doc.containsKey("sensor_active")) {
                    bool enabled = doc["sensor_active"].as<bool>();
                    if (!enabled) {
                        if ((bed.state == BED_RUNNING ||
                             bed.state == BED_PAUSED) &&
                            preparePumpOutputForInactiveState(targetBed)) {
                            bed.state = BED_STANDBY;
                            bed.configurationConfirmed = false;
                        }
                    }
                    bed.isSensorActive = enabled;
                }
                if ((doc["standby"] | false) && bed.state != BED_ALARM) {
                    if (bed.state == BED_STANDBY ||
                        preparePumpOutputForInactiveState(targetBed)) {
                        bed.state = BED_STANDBY;
                        bed.configurationConfirmed = false;
                    }
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
                    if (!xSemaphoreTake(dripMutex, pdMS_TO_TICKS(20))) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;
                    }
                    BedState bedStates[2] = {infus[0].state, infus[1].state};
                    xSemaphoreGive(dripMutex);
                    const char *sysStatus =
                        bedStates[0] == BED_ALARM || bedStates[1] == BED_ALARM
                            ? "ALARM"
                            : bedStates[0] == bedStates[1]
                                  ? bedStateName(bedStates[0])
                                  : "MIXED";

                    // === 1. Publish Bed 1 (topik khusus) ===
                    {
                        DynamicJsonDocument doc1(1024);
                        doc1["device_id"]     = "MONITIS-ESP32";
                        doc1["uptime_sec"]    = now / 1000;
                        doc1["system_status"] = sysStatus;
                        JsonObject b1 = doc1.createNestedObject("bed_data");
                        if (buildBedJson(b1, 0, now)) {
                            String s1; serializeJson(doc1, s1);
                            mqtt.publish(TOPIC_BED1_TELEMETRY, s1.c_str());
                        }
                    }

                    // === 2. Publish Bed 2 (topik khusus) ===
                    {
                        DynamicJsonDocument doc2(1024);
                        doc2["device_id"]     = "MONITIS-ESP32";
                        doc2["uptime_sec"]    = now / 1000;
                        doc2["system_status"] = sysStatus;
                        JsonObject b2 = doc2.createNestedObject("bed_data");
                        if (buildBedJson(b2, 1, now)) {
                            String s2; serializeJson(doc2, s2);
                            mqtt.publish(TOPIC_BED2_TELEMETRY, s2.c_str());
                        }
                    }

                    // === 3. Publish complete global summary ===
                    {
                        DynamicJsonDocument docAll(2048);
                        docAll["device_id"]     = "MONITIS-ESP32";
                        docAll["uptime_sec"]    = now / 1000;
                        docAll["wifi_rssi"]     = WiFi.RSSI();
                        docAll["ip_address"]    = WiFi.localIP().toString();
                        docAll["system_status"] = sysStatus;

                        JsonArray beds = docAll.createNestedArray("beds");
                        bool bedsBuilt = true;
                        for (int i = 0; i < 2; i++) {
                            JsonObject b = beds.createNestedObject();
                            if (!buildBedJson(b, i, now))
                                bedsBuilt = false;
                        }
                        if (bedsBuilt) {
                            String sAll; serializeJson(docAll, sAll);
                            mqtt.publish(TOPIC_ALL_TELEMETRY, sAll.c_str());
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
