#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Five timestamps provide four intervals (~48 s at 5 DPM); a longer window
// is steadier but reacts more slowly to changes.
#define DPM_TIMESTAMP_COUNT 5
#define DPM_STALE_TIMEOUT_MS 15000UL
#define ALARM_RECOVERY_PULSE_COUNT DPM_TIMESTAMP_COUNT
#define NO_FLOW_ALARM_TIMEOUT_MS 15000UL

// No driver, pinout, or calibration has been selected. Keep all pump output
// paths compiled out until the motor and driver datasheets are available.
#define PUMP_OUTPUT_ENABLED 0
#define PIN_PUMP_DRIVER_BED1 -1
#define PIN_PUMP_DRIVER_BED2 -1

// --- PINOUT INPUTS (DUAL-BED SYSTEM: 1 ESP32 -> 2 RANJANG) ---
#define PIN_IR_A 27
#define PIN_IR_B 14

// Shared Inputs & OLED
#define PIN_ENC_CLK 25
#define PIN_ENC_DT 26
#define PIN_ENC_SW 32

// Buzzer untuk alarm audibel
// #define PIN_BUZZER        13  // Buzzer removed per new alarm design

// Helper inline untuk mengaktifkan/mematikan buzzer
// static inline void buzzerOn()  { /* buzzer removed */ }
static inline void buzzerOff() { /* buzzer removed */ }

// Waktu masuk ke screen alarm (digunakan untuk animasi kedip)
extern unsigned long alarmEnterTime;
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22

// --- KONFIGURASI WIFI & MQTT ---
#define WIFI_SSID "POCO M3"
#define WIFI_PASS "244makasihnya"
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASS ""

// Topik MQTT Dual-Bed
#define TOPIC_BED1_TELEMETRY "monitis/bed1/telemetry"
#define TOPIC_BED2_TELEMETRY "monitis/bed2/telemetry"
#define TOPIC_ALL_TELEMETRY  "monitis/telemetry"
#define TOPIC_CONTROL_GLOBAL "monitis/control"
#define TOPIC_CONTROL_BED1   "monitis/bed1/control"
#define TOPIC_CONTROL_BED2   "monitis/bed2/control"

// No-drop alarms indicate absent sensor pulses, not a diagnosed cause.
enum AlarmCode {
  ALARM_NONE = 0,
  ALARM_FLOW_NOT_DETECTED = 1,
  ALARM_PUMP_OUTPUT_FAILURE = 2
};

enum BedState {
  BED_STANDBY = 0,
  BED_RUNNING = 1,
  BED_PAUSED = 2,
  BED_ALARM = 3
};

static inline const char *bedStateName(BedState state) {
  switch (state) {
    case BED_RUNNING: return "RUNNING";
    case BED_PAUSED: return "PAUSED";
    case BED_ALARM: return "ALARM";
    default: return "STANDBY";
  }
}

// --- STRUKTUR DATA CHANNEL INFUS ---
struct DripChannel {
  int id;
  int pinIR;

  // Status Infus
  volatile int targetDPM;
  volatile int actualDPM;
  volatile float sisaVolume;    // ml
  volatile float volumeAwal;    // ml
  volatile BedState state;
  volatile bool isSensorActive; // true = sensor aktif, false = dinonaktifkan (abaikan alarm)
  volatile bool configurationConfirmed;
  volatile bool dpmValid;
  volatile bool dpmStale;
  volatile bool alarmAcknowledged;
  volatile bool alarmRecoveryReady;
  volatile unsigned long lastDropTime;

  // Nominal flow conversions derived from the drop sensor, not direct flow sensors.
  volatile float targetFlowRateMlHr;
  volatile float measuredFlowRateMlHr;
  volatile long etaMinutes;     // Estimasi menit sisa cairan
  volatile int dropFactor;      // 20 tetes/ml (dewasa) atau 60 tetes/ml (anak-anak)
  volatile AlarmCode alarmCode; // Kode spesifik jenis alarm

  // Pump request state; speed conversion belongs to a calibrated driver backend.
  float integral;
  float lastError;
  volatile float pumpCommandFlowRateMlHr;
  volatile bool pumpCommandKnown;
  volatile bool pumpCommandActive;
};

// --- OLED screen state machine ---
enum ScreenState {
  SCREEN_STARTUP = 1,
  SCREEN_SUMMARY = 2,
  SCREEN_DETAIL_A = 3,
  SCREEN_DETAIL_B = 4,
  SCREEN_MAIN_SETTINGS = 5,
  SCREEN_TARGET_DPM_CONFIG = 6,
  SCREEN_VOLUME_CONFIG = 7,
  SCREEN_NETWORK_STATUS = 8,
  SCREEN_SENSOR_DIAGNOSTICS = 9,
  SCREEN_ALARM_HISTORY = 10,
  SCREEN_SYSTEM_INFO = 11,
  SCREEN_ALARM_WARNING = 12,
  SCREEN_CONFIRMATION = 13,
  SCREEN_BED_ACTIONS = 14,
  SCREEN_START_CONFIRMATION = 15,
  SCREEN_ALARM_RESET_CONFIRMATION = 16
};

// Riwayat Alarm sederhana
struct AlarmLog {
  char timeStr[16];
  char desc[24];
};

// Deklarasi Variabel Global Bersama
extern DripChannel infus[2];
extern SemaphoreHandle_t dripMutex;
extern volatile ScreenState currentScreen;
extern ScreenState previousScreen;
extern int selectedChannel;     // 0 = Channel A, 1 = Channel B
extern int bedActionIndex;
extern int settingsMenuIndex;   // Index kursor Main Settings (0 - 5)
extern volatile int alarmChannelTrigger; // Channel yang memicu alarm
extern volatile long pulseCountA;
extern volatile long pulseCountB;
extern AlarmLog alarmHistory[3];

// Deklarasi Task Handles
extern TaskHandle_t TaskControlHandle;

#endif
