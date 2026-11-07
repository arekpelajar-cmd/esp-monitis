#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// --- FITUR SIMULASI TANPA SENSOR FISIK ---
// Set 'true' untuk simulasi otomatis tetesan (cocok untuk testing OLED & MQTT)
// Set 'false' jika menggunakan sensor optik/IR fisik asli pada pin ESP32
#define SIMULATION_MODE false

// --- PINOUT HARDWARE (DUAL-BED SYSTEM: 1 ESP32 -> 2 RANJANG) ---
// BED 1 (Pasien 1)
#define PIN_IR_A 27
#define PIN_SERVO_A 18

// BED 2 (Pasien 2)
#define PIN_IR_B 14
#define PIN_SERVO_B 19

// Shared Inputs & OLED
#define PIN_ENC_CLK 25
#define PIN_ENC_DT 26
#define PIN_ENC_SW 32
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

// --- STRUKTUR DATA CHANNEL INFUS ---
struct DripChannel {
  int id;
  int pinIR;
  int pinServo;

  // Status Infus
  volatile int targetDPM;
  volatile int actualDPM;
  volatile float sisaVolume; // ml
  volatile float volumeAwal; // ml
  volatile bool isPaused;
  volatile bool isAnomaly;
  volatile bool isSensorActive; // true = sensor aktif, false = sensor dinonaktifkan (abaikan alarm)
  volatile unsigned long lastDropTime;

  // PID & Kontrol Servo
  float kp;
  float ki;
  float kd;
  float integral;
  float lastError;
  int servoAngle;
  Servo servo;
};

// --- STATE MACHINE UI OLED (Berdasarkan 13 UI Flow Guide) ---
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
  SCREEN_CONFIRMATION = 13
};

// Riwayat Alarm sederhana
struct AlarmLog {
  char timeStr[16];
  char desc[24];
};

// Deklarasi Variabel Global Bersama
extern DripChannel infus[2];
extern SemaphoreHandle_t dripMutex;
extern ScreenState currentScreen;
extern ScreenState previousScreen;
extern int selectedChannel;     // 0 = Channel A, 1 = Channel B
extern int settingsMenuIndex;   // Index kursor Main Settings (0 - 5)
extern int alarmChannelTrigger; // Channel yang memicu alarm
extern volatile long pulseCountA;
extern volatile long pulseCountB;
extern AlarmLog alarmHistory[3];

// Deklarasi Task Handles
extern TaskHandle_t TaskControlHandle;
extern TaskHandle_t TaskCommsHandle;

#endif
