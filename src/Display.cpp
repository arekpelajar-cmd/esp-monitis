#include "Display.h"
#include "Comms.h"
#include "Config.h"
#include "FlowConversion.h"
#include "PumpOutput.h"
#include "Sensor.h"
#include <Adafruit_GFX.h>
#include "SH1106Display.h"
#include <WiFi.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Kompatibilitas Warna Driver SH1106 (OLED 1.3")
#ifndef SSD1306_WHITE
#define SSD1306_WHITE SH110X_WHITE
#endif
#ifndef SSD1306_BLACK
#define SSD1306_BLACK SH110X_BLACK
#endif

static Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static unsigned long startupBeginTime = 0;
static unsigned long confirmScreenTime = 0;

static const char *bedStateText(BedState state) {
  return bedStateName(state);
}

static const char *dpmStatusText(int ch) {
  if (!infus[ch].isSensorActive)
    return "Sensor OFF";
  if (infus[ch].dpmValid)
    return "Valid";
  if (infus[ch].dpmStale)
    return "Data stale";
  if (infus[ch].state == BED_ALARM) {
    return infus[ch].alarmCode == ALARM_PUMP_OUTPUT_FAILURE
               ? "Gangguan pompa"
               : "Aliran tidak terdeteksi";
  }
  if (infus[ch].state == BED_STANDBY)
    return "STANDBY";
  if (infus[ch].state == BED_PAUSED)
    return "PAUSED";
  return infus[ch].state == BED_RUNNING ? "Mengukur" : "Tidak valid";
}

// ==========================================
// KUMPULAN IKON & SIMBOL GRAFIS (PIXEL PERFECT)
// ==========================================

// 1. Ikon Play (Segitiga Solid)
void drawPlayIcon(int x, int y, uint16_t color) {
  display.fillTriangle(x, y, x, y + 8, x + 6, y + 4, color);
}

// 2. Ikon Pause (Dua Garis Tegak)
void drawPauseIcon(int x, int y, uint16_t color) {
  display.fillRect(x, y, 2, 8, color);
  display.fillRect(x + 4, y, 2, 8, color);
}

// 3. Ikon Target / Crosshair (Lingkaran + Titik Tengah + 4 Garis Bidik)
void drawCrosshairIcon(int cx, int cy, uint16_t color) {
  display.drawCircle(cx, cy, 4, color);
  display.drawPixel(cx, cy, color);
  display.drawLine(cx, cy - 6, cx, cy - 4, color);
  display.drawLine(cx, cy + 4, cx, cy + 6, color);
  display.drawLine(cx - 6, cy, cx - 4, cy, color);
  display.drawLine(cx + 4, cy, cx + 6, cy, color);
}

// 4. Ikon Tetesan Air / Water Droplet
void drawDropIcon(int dx, int dy, uint16_t color) {
  display.fillCircle(dx, dy + 6, 4, color);
  display.fillTriangle(dx, dy, dx - 4, dy + 6, dx + 4, dy + 6, color);
  display.drawPixel(dx - 1, dy + 5,
                    (color == SSD1306_WHITE) ? SSD1306_BLACK : SSD1306_WHITE);
}

// 5. Ikon Tetesan Air Besar dengan Tanda Palang Medis (Startup Screen)
void drawDropCrossIcon(int x, int y) {
  // Tetesan air luar (putih)
  display.fillCircle(x + 10, y + 16, 10, SSD1306_WHITE);
  display.fillTriangle(x + 10, y, x, y + 16, x + 20, y + 16, SSD1306_WHITE);
  // Palang medis di dalam (hitam)
  display.fillRect(x + 8, y + 11, 4, 11, SSD1306_BLACK);
  display.fillRect(x + 5, y + 14, 10, 4, SSD1306_BLACK);
}

// 6. Ikon Segitiga Peringatan + Kilatan Berkedip (Alarm Warning Screen)
void drawWarningTriangle(int cx, int topY, bool showRays) {
  // Segitiga utama (ganda agar tebal)
  display.drawTriangle(cx - 16, topY + 24, cx, topY, cx + 16, topY + 24,
                       SSD1306_WHITE);
  display.drawTriangle(cx - 15, topY + 23, cx, topY + 1, cx + 15, topY + 23,
                       SSD1306_WHITE);

  // Tanda seru (!) di dalam segitiga
  display.drawLine(cx, topY + 8, cx, topY + 16, SSD1306_WHITE);
  display.drawPixel(cx, topY + 19, SSD1306_WHITE);
  display.drawPixel(cx, topY + 20, SSD1306_WHITE);

  // Kilatan animasi di samping kiri & kanan
  if (showRays) {
    // Sisi kiri
    display.drawLine(cx - 24, topY + 7, cx - 19, topY + 11, SSD1306_WHITE);
    display.drawLine(cx - 26, topY + 15, cx - 20, topY + 15, SSD1306_WHITE);
    display.drawLine(cx - 24, topY + 22, cx - 19, topY + 18, SSD1306_WHITE);

    // Sisi kanan
    display.drawLine(cx + 24, topY + 7, cx + 19, topY + 11, SSD1306_WHITE);
    display.drawLine(cx + 26, topY + 15, cx + 20, topY + 15, SSD1306_WHITE);
    display.drawLine(cx + 24, topY + 22, cx + 19, topY + 18, SSD1306_WHITE);
  }
}

// 7. Ikon Roda Gigi / Gear (Pengaturan)
void drawGearIcon(int x, int y, uint16_t color) {
  display.drawCircle(x + 4, y + 4, 4, color);
  display.drawPixel(x + 4, y + 4,
                    (color == SSD1306_WHITE) ? SSD1306_BLACK : SSD1306_WHITE);
  display.drawPixel(x + 4, y, color);
  display.drawPixel(x + 4, y + 8, color);
  display.drawPixel(x, y + 4, color);
  display.drawPixel(x + 8, y + 4, color);
  display.drawPixel(x + 1, y + 1, color);
  display.drawPixel(x + 7, y + 1, color);
  display.drawPixel(x + 1, y + 7, color);
  display.drawPixel(x + 7, y + 7, color);
}

// 8. Ikon WiFi (Gelombang Lengkung)
void drawWifiIcon(int x, int y, uint16_t color) {
  display.fillCircle(x + 6, y + 8, 1, color);
  display.drawLine(x + 4, y + 5, x + 8, y + 5, color);
  display.drawPixel(x + 3, y + 6, color);
  display.drawPixel(x + 9, y + 6, color);
  display.drawLine(x + 2, y + 2, x + 10, y + 2, color);
  display.drawPixel(x + 1, y + 3, color);
  display.drawPixel(x + 11, y + 3, color);
}

// 9. Ikon Cloud / Awan (MQTT)
void drawCloudIcon(int x, int y, uint16_t color) {
  display.drawLine(x + 2, y + 9, x + 11, y + 9, color);
  display.drawCircle(x + 3, y + 7, 2, color);
  display.drawCircle(x + 7, y + 5, 3, color);
  display.drawCircle(x + 10, y + 7, 2, color);
  display.fillRect(x + 2, y + 7, 9, 2, SSD1306_BLACK); // bersihkan garis dalam
}

// 10. Ikon Lingkaran Centang Kecil (Sensor OK)
void drawCheckCircle(int cx, int cy, int r, uint16_t color) {
  display.drawCircle(cx, cy, r, color);
  display.drawLine(cx - 3, cy, cx - 1, cy + 2, color);
  display.drawLine(cx - 1, cy + 2, cx + 3, cy - 2, color);
}

// 11. Ikon Centang Besar Lingkaran (Confirmation Screen)
void drawBigCheckmarkCircle(int cx, int cy, int r) {
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.drawCircle(cx, cy, r - 1, SSD1306_WHITE);
  display.drawLine(cx - 6, cy, cx - 2, cy + 5, SSD1306_WHITE);
  display.drawLine(cx - 6, cy + 1, cx - 2, cy + 6, SSD1306_WHITE);
  display.drawLine(cx - 2, cy + 5, cx + 7, cy - 4, SSD1306_WHITE);
  display.drawLine(cx - 2, cy + 6, cx + 7, cy - 3, SSD1306_WHITE);
}

// 12. Ikon Chip / MCU (System Info)
void drawChipIcon(int x, int y, uint16_t color) {
  display.drawRect(x + 2, y + 2, 8, 8, color);
  display.drawPixel(x + 4, y + 4, color);
  display.drawPixel(x + 7, y + 7, color);
  // Pin kaki chip
  display.drawLine(x + 4, y, x + 4, y + 1, color);
  display.drawLine(x + 7, y, x + 7, y + 1, color);
  display.drawLine(x + 4, y + 10, x + 4, y + 11, color);
  display.drawLine(x + 7, y + 10, x + 7, y + 11, color);
  display.drawLine(x, y + 4, x + 1, y + 4, color);
  display.drawLine(x, y + 7, x + 1, y + 7, color);
  display.drawLine(x + 10, y + 4, x + 11, y + 4, color);
  display.drawLine(x + 10, y + 7, x + 11, y + 7, color);
}

// 13. Panah Atas & Bawah
void drawUpArrow(int cx, int cy, uint16_t color) {
  display.fillTriangle(cx, cy - 3, cx - 4, cy + 2, cx + 4, cy + 2, color);
}

void drawDownArrow(int cx, int cy, uint16_t color) {
  display.fillTriangle(cx, cy + 3, cx - 4, cy - 2, cx + 4, cy - 2, color);
}

// 14. Ikon Panah Putar Balik / Return (Batal)
void drawReturnIcon(int x, int y, uint16_t color) {
  display.drawLine(x + 1, y + 3, x + 4, y, color);
  display.drawLine(x + 1, y + 3, x + 4, y + 6, color);
  display.drawLine(x + 1, y + 3, x + 6, y + 3, color);
  display.drawLine(x + 6, y + 3, x + 6, y + 1, color);
}

// ==========================================
// INISIALISASI OLED
// ==========================================
void initDisplay() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED SH1106 1.3\" Init Failed");
  }
  display.clearDisplay();
  display.display();
  startupBeginTime = millis();
}

// ==========================================
// 13 SCREEN IMPLEMENTASI SESUAI STORYBOARD
// ==========================================

// 1. Startup Screen
void renderStartup() {
  unsigned long elapsed = millis() - startupBeginTime;
  int progress = map(elapsed, 0, 2500, 10, 100);
  if (progress > 100)
    progress = 100;

  // Ikon tetesan air dengan palang medis di kiri
  drawDropCrossIcon(18, 8);

  // Judul Aplikasi
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(48, 14);
  display.print("Smart IV");
  display.setCursor(48, 26);
  display.print("Drip Monitor");

  // Progress Bar Inisialisasi
  display.drawRoundRect(12, 42, 104, 7, 2, SSD1306_WHITE);
  int fillW = (progress * 100) / 100;
  if (fillW > 0) {
    display.fillRoundRect(14, 44, fillW, 3, 1, SSD1306_WHITE);
  }

  // Teks persentase inisialisasi
  display.setCursor(18, 53);
  display.printf("Inisialisasi... %d%%", progress);

  if (elapsed > 2500) {
    currentScreen = SCREEN_SUMMARY;
  }
}

// 2. Summary Dashboard (Ringkasan Status 2 Channel Berdampingan)
void renderSummary() {
  for (int i = 0; i < 2; i++) {
    int x = (i == 0) ? 1 : 65;
    bool isSel = (selectedChannel == i);

    if (isSel) {
      // Channel aktif diberi highlight (latar putih, teks & ikon hitam)
      display.fillRoundRect(x, 0, 62, 64, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.drawLine(x + 2, 47, x + 59, 47, SSD1306_BLACK);
    } else {
      // Channel non-aktif (latar hitam dengan border putih)
      display.drawRoundRect(x, 0, 62, 64, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
      display.drawLine(x + 2, 47, x + 59, 47, SSD1306_WHITE);
    }

    uint16_t fgColor = isSel ? SSD1306_BLACK : SSD1306_WHITE;

    // Label Bed (BED 1 / BED 2)
    display.setTextSize(1);
    display.setCursor(x + 4, 4);
    display.print((i == 0) ? "BED 1" : "BED 2");

    // Alarm marker remains visible on the affected bed.
    if (infus[i].state == BED_ALARM) {
      if (infus[i].alarmAcknowledged || ((millis() / 350) % 2 == 0))
        display.fillTriangle(x + 49, 3, x + 44, 11, x + 54, 11, fgColor);
    }

    display.setTextSize(1);
    display.setCursor(x + 4, 15);
    display.print(infus[i].state == BED_ALARM &&
                          infus[i].alarmAcknowledged
                      ? "ALARM ACK"
                      : bedStateText(infus[i].state));

    display.setTextSize(1);
    display.setCursor(x + 4, 25);
    display.printf("T:%d DPM", infus[i].targetDPM);
    display.setCursor(x + 4, 35);
    if (infus[i].dpmValid)
      display.printf("A:%d DPM", infus[i].actualDPM);
    else if (infus[i].state == BED_ALARM)
      display.print(infus[i].dpmStale ? "Data stale" : "No drop");
    else if (infus[i].dpmStale)
      display.print("Stale");
    else
      display.print(dpmStatusText(i));

    display.setCursor(x + 4, 49);
    display.print(infus[i].pumpCommandActive ? "Cmd: AKTIF" : "Cmd: OFF");
    display.setCursor(x + 4, 57);
    display.printf("Sisa:%dml", (int)infus[i].sisaVolume);
  }
}

// 3 & 4. Detail & Config (Detail Channel A / B)
void renderDetail(int ch) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header [ BED 1 ] atau [ BED 2 ]
  display.setCursor(4, 2);
  display.printf("[ BED %d ]", ch + 1);

  // Garis pemisah header
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);
  display.setCursor(78, 2);
  display.print(bedStateText(infus[ch].state));

  display.setTextSize(1);
  display.setCursor(4, 15);
  display.printf("Target: %d DPM", infus[ch].targetDPM);
  display.setCursor(4, 25);
  if (infus[ch].dropFactor > 0)
    display.printf("Nominal: %.1f ml/j", infus[ch].targetFlowRateMlHr);
  else
    display.print("Nominal: tidak tersedia");
  display.setCursor(4, 35);
  if (infus[ch].dpmValid)
    display.printf("Aktual: %d DPM", infus[ch].actualDPM);
  else if (infus[ch].state == BED_ALARM &&
           infus[ch].alarmCode == ALARM_FLOW_NOT_DETECTED)
    display.print(infus[ch].dpmStale ? "Data stale"
                                     : "Pulsa: tidak ada");
  else
    display.printf("Ukur: %s", dpmStatusText(ch));
  display.setCursor(4, 45);
  if (infus[ch].state == BED_ALARM)
    display.print(infus[ch].alarmAcknowledged ? "Alarm: diakui" : "Alarm: aktif");
  else if (infus[ch].dpmValid)
    display.printf("Estimasi: %.1f ml/j", infus[ch].measuredFlowRateMlHr);
  else
    display.print("Estimasi: belum valid");
  display.setCursor(4, 55);
  if (infus[ch].alarmCode == ALARM_PUMP_OUTPUT_FAILURE)
    display.print("Pompa: perintah gagal");
  else
    display.print(infus[ch].pumpCommandActive ? "Perintah pompa: AKTIF"
                                               : "Perintah pompa: OFF");
  display.setCursor(4, 62);
  display.print("Klik: aksi bed");
}

static void renderBedActions() {
  int ch = selectedChannel;
  const char *actions[3];
  int count = 3;
  if (infus[ch].state == BED_STANDBY) {
    actions[0] = "MULAI bed ini";
    actions[1] = "Kembali";
    count = 2;
  } else if (infus[ch].state == BED_RUNNING) {
    actions[0] = "PAUSE";
    actions[1] = "STOP ke STANDBY";
    actions[2] = "Kembali";
  } else if (infus[ch].state == BED_PAUSED) {
    actions[0] = "MULAI LAGI";
    actions[1] = "STOP ke STANDBY";
    actions[2] = "Kembali";
  } else {
    actions[0] = infus[ch].alarmAcknowledged ? "Alarm diakui" : "Akui alarm";
    actions[1] = infus[ch].alarmAcknowledged
                     ? (infus[ch].alarmRecoveryReady ? "Reset ke STANDBY"
                                                     : "Tunggu 5 tetes valid")
                     : "Akui dahulu";
    actions[2] = "Kembali";
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);
  display.printf("BED %d - %s", ch + 1, bedStateText(infus[ch].state));
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);
  for (int i = 0; i < count; i++) {
    int y = 19 + i * 13;
    if (i == bedActionIndex) {
      display.fillRoundRect(2, y - 1, 122, 11, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(6, y + 1);
    display.print(actions[i]);
  }
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 57);
  display.print("Putar pilih, klik OK");
}

static void renderStartConfirmation() {
  int ch = selectedChannel;
  PumpControlProfile profile;
  const bool pumpReady = isPumpOutputReady(ch, &profile);
  const bool baseChecksPass = infus[ch].isSensorActive &&
                    infus[ch].targetDPM >= 5 && infus[ch].targetDPM <= 150 &&
                    infus[ch].dropFactor > 0 && infus[ch].volumeAwal > 0.0f;
  float nominalTarget = 0.0f;
  dpmToNominalFlowMlHr(infus[ch].targetDPM, infus[ch].dropFactor,
                       &nominalTarget);
  const bool rateInRange =
      pumpReady && nominalTarget >= profile.minFlowMlHr &&
      nominalTarget <= profile.maxFlowMlHr;
  const bool checksPass = baseChecksPass && rateInRange;
  const char *checkStatus =
      !infus[ch].isSensorActive
          ? "Sensor nonaktif"
          : infus[ch].targetDPM < 5 || infus[ch].targetDPM > 150 ||
                    infus[ch].dropFactor <= 0 || infus[ch].volumeAwal <= 0.0f
                ? "Periksa konfigurasi"
                : !pumpReady ? "Driver belum siap"
                             : !rateInRange ? "Target di luar kal."
                                            : "Driver/kalibrasi siap";
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 2);
  display.printf("KONFIRMASI MULAI BED %d", ch + 1);
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setCursor(4, 16);
  display.printf("Target: %d DPM", infus[ch].targetDPM);
  display.setCursor(4, 27);
  display.printf("Nominal: %.1f ml/j", nominalTarget);
  display.setCursor(4, 38);
  display.printf("F:%d V:%d Sensor:%s", infus[ch].dropFactor,
                 (int)infus[ch].volumeAwal,
                 infus[ch].isSensorActive ? "ON" : "OFF");
  display.setCursor(0, 47);
  display.print(checkStatus);
  display.setCursor(0, 56);
  display.print(checksPass ? "Klik OK: MULAI" : "Output pompa terkunci");
}

static void renderAlarmResetConfirmation() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 5);
  display.printf("RESET ALARM BED %d?", selectedChannel + 1);
  display.drawLine(0, 16, 128, 16, SSD1306_WHITE);
  display.setCursor(0, 23);
  display.print(infus[selectedChannel].alarmRecoveryReady
                    ? "5 tetes valid: cukup"
                    : "5 tetes valid: kurang");
  display.setCursor(0, 35);
  display.print("Inspeksi sebelum reset.");
  display.setCursor(0, 47);
  display.print("Reset bukan tes aliran.");
  display.setCursor(4, 59);
  display.print("Klik: reset alarm");
}

// 5. Main Settings (Menu Utama)
void renderMainSettings() {
  // Header dengan Ikon Gear
  drawGearIcon(6, 2, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 3);
  display.print("Pengaturan");
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);

  const char *menus[7] = {"Target DPM Bed 1",  "Target DPM Bed 2",
                          "Volume Settings",   "Alarm Settings",
                          "Network Settings",  "Sensor Diagnostics",
                          "System Info"};

  int topIdx = 0;
  if (settingsMenuIndex >= 3) {
    topIdx = settingsMenuIndex - 3;
  }
  if (topIdx > 3)
    topIdx = 3;

  int startY = 16;
  for (int r = 0; r < 4; r++) {
    int idx = topIdx + r;
    int y = startY + (r * 12);

    if (idx == settingsMenuIndex) {
      display.fillRoundRect(2, y, 118, 11, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(4, y + 2);
      display.print(menus[idx]);
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(6, y + 2);
      display.print(menus[idx]);
    }
  }

  // Scrollbar di sisi kanan
  display.drawRect(123, 16, 3, 46, SSD1306_WHITE);
  int thumbH = 10;
  int thumbY = 16 + (settingsMenuIndex * (46 - thumbH)) / 6;
  display.fillRect(123, thumbY, 3, thumbH, SSD1306_WHITE);
}

// 6. Target DPM Config
void renderTargetConfig() {
  // Header
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 2);
  display.printf("Target DPM Bed %d", selectedChannel + 1);
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);

  // Panah Atas
  drawUpArrow(64, 18, SSD1306_WHITE);

  // Nilai [ 20 ]
  display.setTextSize(2);
  display.setCursor(32, 27);
  display.printf("[ %2d ]", infus[selectedChannel].targetDPM);

  // Panah Bawah
  drawDownArrow(64, 47, SSD1306_WHITE);

  // Footer Navigasi
  display.setTextSize(1);
  display.setCursor(6, 54);
  if (infus[selectedChannel].state == BED_STANDBY) {
    display.print("Putar ubah target");
  } else {
    display.print("Ubah hanya saat STBY");
  }
}

// 7. Volume Configuration
void renderVolumeConfig() {
  // Header
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 2);
  display.printf("Volume Awal Bed %d", selectedChannel + 1);
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);

  // Panah Atas
  drawUpArrow(64, 18, SSD1306_WHITE);

  // Nilai [ 500 ] ml
  display.setTextSize(2);
  display.setCursor(14, 27);
  display.printf("[ %3d ]", (int)infus[selectedChannel].volumeAwal);
  display.setTextSize(1);
  display.setCursor(102, 33);
  display.print("ml");

  // Panah Bawah
  drawDownArrow(64, 47, SSD1306_WHITE);

  // Footer Navigasi
  display.setTextSize(1);
  display.setCursor(6, 54);
  if (infus[selectedChannel].state == BED_STANDBY) {
    display.print("Putar ubah volume");
  } else {
    display.print("Ubah hanya saat STBY");
  }
}

// 8. Network Status
void renderNetworkStatus() {
  // WiFi Status
  drawWifiIcon(8, 6, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(26, 7);
  display.print("WiFi : ");
  display.print((WiFi.status() == WL_CONNECTED) ? "Connected" : "Offline");

  // MQTT Status
  drawCloudIcon(8, 22, SSD1306_WHITE);
  display.setCursor(26, 23);
  display.print("MQTT : ");
  display.print(isMqttConnected() ? "Connected" : "Offline");

  // Garis Pemisah
  display.drawLine(4, 38, 124, 38, SSD1306_WHITE);

  // IP Address
  display.setCursor(8, 46);
  display.print("IP : ");
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP().toString());
  } else {
    display.print("192.168.1.100");
  }
}

// 9. Sensor Diagnostics
void renderDiagnostics() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 6);
  display.print("B1 DPM:");
  if (!infus[0].isSensorActive)
    display.print("Sensor OFF");
  else if (infus[0].dpmValid)
    display.print(infus[0].actualDPM);
  else if (infus[0].state == BED_ALARM &&
           infus[0].alarmCode == ALARM_FLOW_NOT_DETECTED)
    display.print(infus[0].dpmStale ? "Stale" : "No drop");
  else
    display.print(dpmStatusText(0));
  display.setCursor(4, 19);
  display.print("B2 DPM:");
  if (!infus[1].isSensorActive)
    display.print("Sensor OFF");
  else if (infus[1].dpmValid)
    display.print(infus[1].actualDPM);
  else if (infus[1].state == BED_ALARM &&
           infus[1].alarmCode == ALARM_FLOW_NOT_DETECTED)
    display.print(infus[1].dpmStale ? "Stale" : "No drop");
  else
    display.print(dpmStatusText(1));

  // Garis Pemisah
  display.drawLine(4, 33, 124, 33, SSD1306_WHITE);

  // Pulse Count
  display.setCursor(16, 39);
  display.printf("Pulse Bed 1 : %ld", getPulseCount_Safe(0));
  display.setCursor(16, 51);
  display.printf("Pulse Bed 2 : %ld", getPulseCount_Safe(1));
}

// 10. Alarm History
void renderAlarmHistory() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(24, 2);
  display.print("Riwayat Alarm");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

  for (int i = 0; i < 3; i++) {
    int y = 17 + (i * 14);
    display.setCursor(2, y);
    display.print(alarmHistory[i].timeStr);
    display.setCursor(68, y);
    display.print(alarmHistory[i].desc);
  }
}

// 11. System Information
void renderSystemInfo() {
  // Ikon Chip & Versi
  drawChipIcon(8, 2, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(26, 4);
  display.print("v1.0.0");
  display.drawLine(0, 14, 128, 14, SSD1306_WHITE);

  // Uptime terformat HH:MM:SS
  unsigned long totalSec = millis() / 1000;
  unsigned long hrs = totalSec / 3600;
  unsigned long mins = (totalSec % 3600) / 60;
  unsigned long secs = totalSec % 60;

  display.setCursor(6, 20);
  display.printf("Uptime : %02lu:%02lu:%02lu", hrs, mins, secs);

  display.setCursor(6, 34);
  display.printf("Heap   : %d KB", ESP.getFreeHeap() / 1024);

  display.setCursor(6, 48);
  display.printf("MQTT   : %s", isMqttConnected() ? "Online" : "Offline");
}

// 12. Alarm Warning (Peringatan Kondisi Darurat)
void renderAlarmWarning() {
  // Ikon Segitiga berkedip tiap 350ms
  bool showRays = ((millis() / 350) % 2 == 0);
  drawWarningTriangle(64, 2, showRays);

  // Teks Peringatan Darurat
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  if (alarmChannelTrigger >= 0 && alarmChannelTrigger < 2) {
    display.setCursor(20, 30);
    display.printf("ALARM BED %d", alarmChannelTrigger + 1);
  }
  if (alarmChannelTrigger >= 0 && alarmChannelTrigger < 2 &&
      infus[alarmChannelTrigger].alarmCode == ALARM_PUMP_OUTPUT_FAILURE) {
    display.setCursor(12, 42);
    display.print("Gangguan output pompa");
    display.setCursor(18, 50);
    display.print("Periksa driver");
  } else {
    display.setCursor(22, 42);
    display.print("Aliran tidak");
    display.setCursor(34, 50);
    display.print("terdeteksi");
  }
  display.setCursor(42, 57);
  display.print("Klik: akui");
}

// 13. Confirmation (Pengaturan Disimpan!)
void renderConfirmation() {
  if (confirmScreenTime == 0) {
    confirmScreenTime = millis();
  }

  // Centang besar di dalam lingkaran
  drawBigCheckmarkCircle(64, 18, 13);

  // Teks Pengaturan Disimpan!
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(32, 38);
  display.print("Pengaturan");
  display.setCursor(36, 50);
  display.print("Disimpan!");

  // Otomatis kembali ke Menu Pengaturan setelah 1.5 detik
  if (millis() - confirmScreenTime > 1500) {
    confirmScreenTime = 0;
    currentScreen = SCREEN_MAIN_SETTINGS;
  }
}

// ==========================================
// LOOP RENDER DISPLAY UTAMA
// ==========================================
void updateDisplay() {
  if (dripMutex != NULL && !xSemaphoreTake(dripMutex, pdMS_TO_TICKS(20)))
    return;

  display.clearDisplay();

  switch (currentScreen) {
  case SCREEN_STARTUP:
    renderStartup();
    break;
  case SCREEN_SUMMARY:
    renderSummary();
    break;
  case SCREEN_DETAIL_A:
    renderDetail(0);
    break;
  case SCREEN_DETAIL_B:
    renderDetail(1);
    break;
  case SCREEN_MAIN_SETTINGS:
    renderMainSettings();
    break;
  case SCREEN_TARGET_DPM_CONFIG:
    renderTargetConfig();
    break;
  case SCREEN_VOLUME_CONFIG:
    renderVolumeConfig();
    break;
  case SCREEN_NETWORK_STATUS:
    renderNetworkStatus();
    break;
  case SCREEN_SENSOR_DIAGNOSTICS:
    renderDiagnostics();
    break;
  case SCREEN_ALARM_HISTORY:
    renderAlarmHistory();
    break;
  case SCREEN_SYSTEM_INFO:
    renderSystemInfo();
    break;
  case SCREEN_ALARM_WARNING:
    renderAlarmWarning();
    break;
  case SCREEN_CONFIRMATION:
    renderConfirmation();
    break;
  case SCREEN_BED_ACTIONS:
    renderBedActions();
    break;
  case SCREEN_START_CONFIRMATION:
    renderStartConfirmation();
    break;
  case SCREEN_ALARM_RESET_CONFIRMATION:
    renderAlarmResetConfirmation();
    break;
  }

  if (dripMutex != NULL)
    xSemaphoreGive(dripMutex);
  display.display();
}
