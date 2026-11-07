#include "Sensor.h"
#include "Config.h"

// Variabel Interrupt Sensor IR
static volatile int dropCountA = 0;
static volatile int dropCountB = 0;
static portMUX_TYPE dropMuxA = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE dropMuxB = portMUX_INITIALIZER_UNLOCKED;

// Variabel Encoder State
static volatile int encCounterDelta = 0;
static volatile bool encButtonClicked = false;
static volatile bool encButtonLongPressed = false;
static unsigned long buttonPressStart = 0;
static bool buttonIsPressed = false;
static int lastClkState = HIGH;

// ISR Sensor IR Channel A
void IRAM_ATTR irDropISR_A() {
    unsigned long now = millis();
    if (now - infus[0].lastDropTime > 50) { // Debounce 50ms
        portENTER_CRITICAL_ISR(&dropMuxA);
        dropCountA = dropCountA + 1;
        pulseCountA = pulseCountA + 1;
        infus[0].lastDropTime = now;
        portEXIT_CRITICAL_ISR(&dropMuxA);
    }
}

// ISR Sensor IR Channel B
void IRAM_ATTR irDropISR_B() {
    unsigned long now = millis();
    if (now - infus[1].lastDropTime > 50) { // Debounce 50ms
        portENTER_CRITICAL_ISR(&dropMuxB);
        dropCountB = dropCountB + 1;
        pulseCountB = pulseCountB + 1;
        infus[1].lastDropTime = now;
        portEXIT_CRITICAL_ISR(&dropMuxB);
    }
}

// ISR Rotary Encoder (Pin CLK / DT)
void IRAM_ATTR encoderRotateISR() {
    int clk = digitalRead(PIN_ENC_CLK);
    int dt  = digitalRead(PIN_ENC_DT);
    if (clk != lastClkState && clk == LOW) {
        if (dt != clk) {
            encCounterDelta = encCounterDelta + 1; // Putar Kanan (Next / Naik)
        } else {
            encCounterDelta = encCounterDelta - 1; // Putar Kiri (Prev / Turun)
        }
    }
    lastClkState = clk;
}

void initSensors() {
    pinMode(PIN_IR_A, INPUT_PULLUP);
    pinMode(PIN_IR_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_IR_A), irDropISR_A, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_IR_B), irDropISR_B, FALLING);

    pinMode(PIN_ENC_CLK, INPUT_PULLUP);
    pinMode(PIN_ENC_DT, INPUT_PULLUP);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    lastClkState = digitalRead(PIN_ENC_CLK);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderRotateISR, CHANGE);
}

int getDropCount_Safe(int ch) {
    int drops = 0;
    if (ch == 0) {
        portENTER_CRITICAL(&dropMuxA);
        drops = dropCountA;
        dropCountA = 0;
        portEXIT_CRITICAL(&dropMuxA);
    } else {
        portENTER_CRITICAL(&dropMuxB);
        drops = dropCountB;
        dropCountB = 0;
        portEXIT_CRITICAL(&dropMuxB);
    }
    return drops;
}

// Handler input tombol encoder & navigasi state machine
void handleEncoderInput() {
    // 1. Deteksi Tombol (Tekan Singkat vs Tekan Tahan)
    bool currentBtnState = (digitalRead(PIN_ENC_SW) == LOW);
    if (currentBtnState && !buttonIsPressed) {
        buttonIsPressed = true;
        buttonPressStart = millis();
    } else if (!currentBtnState && buttonIsPressed) {
        unsigned long duration = millis() - buttonPressStart;
        buttonIsPressed = false;
        if (duration >= 1000) {
            encButtonLongPressed = true;
        } else if (duration >= 40) {
            encButtonClicked = true;
        }
    }

    // Ambil delta putaran encoder
    int delta = 0;
    if (encCounterDelta != 0) {
        delta = encCounterDelta;
        encCounterDelta = 0;
    }

    bool clicked = encButtonClicked;
    bool longPressed = encButtonLongPressed;
    encButtonClicked = false;
    encButtonLongPressed = false;

    // Aksi Universal: Tekan Tahan
    if (longPressed) {
        if (currentScreen == SCREEN_SUMMARY) {
            // Dari Dashboard, Tekan Lama masuk ke Menu Utama (Pengaturan)
            currentScreen = SCREEN_MAIN_SETTINGS;
            return;
        } else if (currentScreen != SCREEN_STARTUP) {
            // Dari Menu manapun, Tekan Lama kembali ke Dashboard
            currentScreen = SCREEN_SUMMARY;
            return;
        }
    }

    // Aksi Navigasi Spesifik per Screen
    switch (currentScreen) {
        case SCREEN_STARTUP:
            // Otomatis berpindah dari timer tampilan
            break;

        case SCREEN_SUMMARY:
            if (delta != 0) {
                // Navigasi Kursor A <-> B
                selectedChannel = (selectedChannel == 0) ? 1 : 0;
            }
            if (clicked) {
                // Masuk ke Detail Channel yang dipilih
                currentScreen = (selectedChannel == 0) ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
            }
            break;

        case SCREEN_DETAIL_A:
        case SCREEN_DETAIL_B: {
            int ch = (currentScreen == SCREEN_DETAIL_A) ? 0 : 1;
            if (delta != 0) {
                // Sesuaikan Target DPM secara cepat
                if (xSemaphoreTake(dripMutex, (TickType_t)10)) {
                    infus[ch].targetDPM += delta;
                    if (infus[ch].targetDPM < 5) infus[ch].targetDPM = 5;
                    if (infus[ch].targetDPM > 150) infus[ch].targetDPM = 150;
                    xSemaphoreGive(dripMutex);
                }
            }
            if (clicked) {
                // Toggle Pause / Resume
                if (xSemaphoreTake(dripMutex, (TickType_t)10)) {
                    infus[ch].isPaused = !infus[ch].isPaused;
                    xSemaphoreGive(dripMutex);
                }
            }
            break;
        }

        case SCREEN_MAIN_SETTINGS:
            if (delta != 0) {
                settingsMenuIndex += delta;
                if (settingsMenuIndex < 0) settingsMenuIndex = 6;
                if (settingsMenuIndex > 6) settingsMenuIndex = 0;
            }
            if (clicked) {
                if (settingsMenuIndex == 0) { selectedChannel = 0; currentScreen = SCREEN_TARGET_DPM_CONFIG; }
                else if (settingsMenuIndex == 1) { selectedChannel = 1; currentScreen = SCREEN_TARGET_DPM_CONFIG; }
                else if (settingsMenuIndex == 2) currentScreen = SCREEN_VOLUME_CONFIG;
                else if (settingsMenuIndex == 3) currentScreen = SCREEN_ALARM_HISTORY;
                else if (settingsMenuIndex == 4) currentScreen = SCREEN_NETWORK_STATUS;
                else if (settingsMenuIndex == 5) currentScreen = SCREEN_SENSOR_DIAGNOSTICS;
                else if (settingsMenuIndex == 6) currentScreen = SCREEN_SYSTEM_INFO;
            }
            break;

        case SCREEN_TARGET_DPM_CONFIG:
            if (delta != 0) {
                if (xSemaphoreTake(dripMutex, (TickType_t)10)) {
                    infus[selectedChannel].targetDPM += delta;
                    if (infus[selectedChannel].targetDPM < 5) infus[selectedChannel].targetDPM = 5;
                    if (infus[selectedChannel].targetDPM > 150) infus[selectedChannel].targetDPM = 150;
                    xSemaphoreGive(dripMutex);
                }
            }
            if (clicked) {
                currentScreen = SCREEN_CONFIRMATION;
            }
            break;

        case SCREEN_VOLUME_CONFIG:
            if (delta != 0) {
                if (xSemaphoreTake(dripMutex, (TickType_t)10)) {
                    infus[selectedChannel].volumeAwal += (delta * 50);
                    if (infus[selectedChannel].volumeAwal < 50) infus[selectedChannel].volumeAwal = 50;
                    if (infus[selectedChannel].volumeAwal > 2000) infus[selectedChannel].volumeAwal = 2000;
                    infus[selectedChannel].sisaVolume = infus[selectedChannel].volumeAwal;
                    xSemaphoreGive(dripMutex);
                }
            }
            if (clicked) {
                currentScreen = SCREEN_CONFIRMATION;
            }
            break;

        case SCREEN_NETWORK_STATUS:
        case SCREEN_SENSOR_DIAGNOSTICS:
        case SCREEN_ALARM_HISTORY:
        case SCREEN_SYSTEM_INFO:
            if (clicked) {
                currentScreen = SCREEN_MAIN_SETTINGS;
            }
            break;

        case SCREEN_ALARM_WARNING:
            if (clicked) {
                // Acknowledge alarm, pulihkan ke Detail Channel yang bermasalah
                if (xSemaphoreTake(dripMutex, (TickType_t)10)) {
                    if (alarmChannelTrigger >= 0) {
                        infus[alarmChannelTrigger].isAnomaly = false;
                        infus[alarmChannelTrigger].lastDropTime = millis(); // Reset timer agar tidak re-trigger instan
                        selectedChannel = alarmChannelTrigger;
                        currentScreen = (alarmChannelTrigger == 0) ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                    } else {
                        currentScreen = SCREEN_SUMMARY;
                    }
                    xSemaphoreGive(dripMutex);
                }
            }
            break;

        case SCREEN_CONFIRMATION:
            if (clicked) {
                currentScreen = SCREEN_MAIN_SETTINGS;
            }
            break;
    }
}
