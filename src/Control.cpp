#include "Control.h"
#include "Config.h"
#include "Sensor.h"

// === Helper: Hitung field telemetri medis per channel ===
// Dipanggil setiap kali DPM atau sisaVolume berubah
static void updateMedicalFields(int i, unsigned long now) {
  // 1. Flow Rate (ml/jam) = actualDPM / dropFactor * 60
  if (infus[i].actualDPM > 0 && infus[i].dropFactor > 0) {
    infus[i].flowRateMlHr = ((float)infus[i].actualDPM / (float)infus[i].dropFactor) * 60.0f;
  } else {
    infus[i].flowRateMlHr = 0.0f;
  }

  // 2. ETA (menit sisa) = (sisaVolume * dropFactor) / actualDPM
  if (infus[i].actualDPM > 0 && infus[i].dropFactor > 0 && infus[i].sisaVolume > 0.0f) {
    infus[i].etaMinutes = (long)((infus[i].sisaVolume * (float)infus[i].dropFactor) / (float)infus[i].actualDPM);
  } else {
    infus[i].etaMinutes = 0L;
  }

  // 3. AlarmCode: Klasifikasi jenis anomali
  if (!infus[i].isAnomaly) {
    infus[i].alarmCode = ALARM_NONE;
  } else if (infus[i].sisaVolume <= 10.0f) {
    infus[i].alarmCode = ALARM_EMPTY;      // Cairan hampir/sudah habis
  } else {
    infus[i].alarmCode = ALARM_OCCLUSION;  // Mampet / selang tertekuk
  }
}

void initControl() {
  for (int i = 0; i < 2; i++) {
    infus[i].servo.attach(infus[i].pinServo, 500, 2400);
    infus[i].servoAngle = infus[i].isPaused ? 0 : 90;
    infus[i].servo.write(infus[i].servoAngle);
    infus[i].lastDropTime = millis();
    infus[i].flowRateMlHr = 0.0f;
    infus[i].etaMinutes   = 0L;
    infus[i].alarmCode    = ALARM_NONE;
  }
}

void taskControlCode(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(100); // 100ms cycle (10Hz)
  unsigned long lastDpmCalcTime = millis();

  for (;;) {
    unsigned long now = millis();

    // 1. Perhitungan Aktual DPM dan Sisa Volume (Tiap 1 Detik)
#if SIMULATION_MODE
    // Mode Simulasi Software (Aktif jika belum ada sensor fisik IR)
    if (now - lastDpmCalcTime >= 1000) {
      for (int i = 0; i < 2; i++) {
        if (!infus[i].isPaused && infus[i].targetDPM > 0) {
          // Beri variasi realistis (+- 1 DPM) di sekitar target
          int jitter = random(-1, 2);
          infus[i].actualDPM = infus[i].targetDPM + jitter;
          if (infus[i].actualDPM < 0)
            infus[i].actualDPM = 0;

          // Pengurangan sisa volume berdasarkan actual DPM
          float dropsPerSec = (float)infus[i].actualDPM / 60.0f;
          infus[i].sisaVolume -= (dropsPerSec / (float)infus[i].dropFactor);
          if (infus[i].sisaVolume < 0)
            infus[i].sisaVolume = 0;

          infus[i].lastDropTime = now;
          infus[i].isAnomaly = false;

          if (i == 0)
            pulseCountA = pulseCountA + (long)(dropsPerSec + 0.5f);
          else
            pulseCountB = pulseCountB + (long)(dropsPerSec + 0.5f);
        } else {
          infus[i].actualDPM = 0;
        }
        updateMedicalFields(i, now);
      }
      lastDpmCalcTime = now;
    }
#else
    // Mode Normal (Menggunakan Pembacaan Sensor IR Fisik)
    if (now - lastDpmCalcTime >= 1000) {
      for (int i = 0; i < 2; i++) {
        int drops = getDropCount_Safe(i);
        infus[i].actualDPM = drops * 60;

        if (!infus[i].isPaused && drops > 0) {
          infus[i].sisaVolume -= ((float)drops / (float)infus[i].dropFactor);
          if (infus[i].sisaVolume < 0)
            infus[i].sisaVolume = 0;
        }

        // 2. Deteksi Anomali (>15 Detik Tanpa Tetesan saat tidak di-pause dan sensor aktif)
        if (infus[i].isSensorActive && !infus[i].isPaused && (now - infus[i].lastDropTime > 15000)) {
          infus[i].isAnomaly = true;
          alarmChannelTrigger = i;
          // Trigger interupsi layar darurat ke Core 0
          if (currentScreen != SCREEN_ALARM_WARNING) {
            currentScreen = SCREEN_ALARM_WARNING;
          }
        }
        updateMedicalFields(i, now);
      }
      lastDpmCalcTime = now;
    }
#endif

    // 3. Algoritma PID & Kontrol Servo per Channel
    for (int i = 0; i < 2; i++) {
      if (infus[i].isPaused || infus[i].isAnomaly) {
        // Tutup klem selang penuh
        infus[i].servoAngle = 0;
        infus[i].servo.write(0);
        infus[i].integral = 0;
      } else {
        int target = 0;
        if (xSemaphoreTake(dripMutex, (TickType_t)5)) {
          target = infus[i].targetDPM;
          xSemaphoreGive(dripMutex);
        }

        float error = (float)target - (float)infus[i].actualDPM;
        infus[i].integral += error * 0.1f;
        // Anti-windup
        if (infus[i].integral > 45)
          infus[i].integral = 45;
        if (infus[i].integral < -45)
          infus[i].integral = -45;

        float derivative = (error - infus[i].lastError) / 0.1f;
        float output = (infus[i].kp * error) +
                       (infus[i].ki * infus[i].integral) +
                       (infus[i].kd * derivative);
        infus[i].lastError = error;

        infus[i].servoAngle += (int)output;
        if (infus[i].servoAngle > 180)
          infus[i].servoAngle = 180;
        if (infus[i].servoAngle < 0)
          infus[i].servoAngle = 0;

        infus[i].servo.write(infus[i].servoAngle);
      }
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
