#include <Arduino.h>
#include "Config.h"
#include "Sensor.h"
#include "Control.h"
#include "Comms.h"
#include "Display.h"

// Inisialisasi Data Infus (Dual-Channel)
// Format struct: id, pinIR, targetDPM, actualDPM, volume,
//                state, sensor/configuration/DPM/alarm flags, timestamps,
//                nominal flow conversions, drop factor/alarm, controller state.
DripChannel infus[2] = {
    {0, PIN_IR_A, 20, 0, 450.0f, 500.0f,
     BED_STANDBY, true, false, false, false, false, false, 0,
     0.0f, 0.0f, 0L, 20, ALARM_NONE, 0.0f, 0.0f, 0.0f, false, false},
    {1, PIN_IR_B, 15, 0, 120.0f, 500.0f,
     BED_STANDBY, true, false, false, false, false, false, 0,
     0.0f, 0.0f, 0L, 20, ALARM_NONE, 0.0f, 0.0f, 0.0f, false, false}
};

SemaphoreHandle_t dripMutex = NULL;
volatile ScreenState currentScreen = SCREEN_STARTUP;
ScreenState previousScreen = SCREEN_SUMMARY;
int selectedChannel = 0;
int bedActionIndex = 0;
int settingsMenuIndex = 0;
volatile int alarmChannelTrigger = -1;
volatile long pulseCountA = 0;
volatile long pulseCountB = 0;

AlarmLog alarmHistory[3] = {
    {"06-11 14:32", "BED 2 No Flow"},
    {"06-11 13:50", "BED 1 Flow Check"},
    {"06-11 12:10", "System Reset"}
};

TaskHandle_t TaskControlHandle = NULL;
TaskHandle_t TaskCommsHandle = NULL;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Smart IV Drip Monitor (Dual-Channel) Starting ===");

    // Inisialisasi Mutex untuk Thread Safety
    dripMutex = xSemaphoreCreateMutex();
    if (dripMutex == NULL) {
        Serial.println("FATAL: Gagal membuat Mutex!");
        while (1);
    }

    // Inisialisasi Modul-Modul
    initSensors();
    initControl();
    initDisplay();
    initComms();

    // Buat TaskControl di Core 1 (Prioritas 2 - Real-Time Control & PID)
    xTaskCreatePinnedToCore(
        taskControlCode,
        "TaskControl",
        10000,
        NULL,
        2,
        &TaskControlHandle,
        1
    );

    // Buat TaskComms di Core 0 (Prioritas 1 - UI OLED & MQTT Network)
    xTaskCreatePinnedToCore(
        taskCommsCode,
        "TaskComms",
        10000,
        NULL,
        1,
        &TaskCommsHandle,
        0
    );
}

void loop() {
    // Loop kosong, eksekusi diserahkan sepenuhnya ke FreeRTOS
    vTaskDelete(NULL);
}
