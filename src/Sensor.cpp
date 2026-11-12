#include "Sensor.h"
#include "Config.h"
#include "Control.h"
#include "FlowConversion.h"
#include "PumpOutput.h"

// Variabel Interrupt Sensor IR
static volatile int dropCountA = 0;
static volatile int dropCountB = 0;
static portMUX_TYPE dropMuxA = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE dropMuxB = portMUX_INITIALIZER_UNLOCKED;
static uint32_t dropTimestamps[2][DPM_TIMESTAMP_COUNT] = {};
static size_t dropTimestampHead[2] = {};
static size_t dropTimestampCount[2] = {};
static uint32_t dropSequence[2] = {};
static bool hasAcceptedDrop[2] = {};

// Variabel Encoder State
static volatile int encCounterDelta = 0;
static volatile bool encButtonClicked = false;
static volatile bool encButtonLongPressed = false;
static portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
static unsigned long buttonPressStart = 0;
static bool buttonIsPressed = false;
static int lastClkState = HIGH;

// ISR Sensor IR Channel A
void IRAM_ATTR irDropISR_A() {
    uint32_t now = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
    portENTER_CRITICAL_ISR(&dropMuxA);
    if (!hasAcceptedDrop[0] || (uint32_t)(now - infus[0].lastDropTime) > 50U) {
        hasAcceptedDrop[0] = true;
        dropCountA = dropCountA + 1;
        pulseCountA = pulseCountA + 1;
        infus[0].lastDropTime = now;
        dropTimestamps[0][dropTimestampHead[0]] = now;
        dropTimestampHead[0] = (dropTimestampHead[0] + 1U) % DPM_TIMESTAMP_COUNT;
        if (dropTimestampCount[0] < DPM_TIMESTAMP_COUNT)
            dropTimestampCount[0]++;
        dropSequence[0]++;
    }
    portEXIT_CRITICAL_ISR(&dropMuxA);
}

// ISR Sensor IR Channel B
void IRAM_ATTR irDropISR_B() {
    uint32_t now = (uint32_t)(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS);
    portENTER_CRITICAL_ISR(&dropMuxB);
    if (!hasAcceptedDrop[1] || (uint32_t)(now - infus[1].lastDropTime) > 50U) {
        hasAcceptedDrop[1] = true;
        dropCountB = dropCountB + 1;
        pulseCountB = pulseCountB + 1;
        infus[1].lastDropTime = now;
        dropTimestamps[1][dropTimestampHead[1]] = now;
        dropTimestampHead[1] = (dropTimestampHead[1] + 1U) % DPM_TIMESTAMP_COUNT;
        if (dropTimestampCount[1] < DPM_TIMESTAMP_COUNT)
            dropTimestampCount[1]++;
        dropSequence[1]++;
    }
    portEXIT_CRITICAL_ISR(&dropMuxB);
}

// ISR Rotary Encoder (Pin CLK / DT)
void IRAM_ATTR encoderRotateISR() {
    int clk = digitalRead(PIN_ENC_CLK);
    int dt  = digitalRead(PIN_ENC_DT);
    portENTER_CRITICAL_ISR(&encoderMux);
    if (clk != lastClkState && clk == LOW) {
        if (dt != clk) {
            encCounterDelta = encCounterDelta + 1; // Putar Kanan (Next / Naik)
        } else {
            encCounterDelta = encCounterDelta - 1; // Putar Kiri (Prev / Turun)
        }
    }
    lastClkState = clk;
    portEXIT_CRITICAL_ISR(&encoderMux);
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

size_t getDropTimestamps_Safe(int ch, uint32_t *timestamps, size_t capacity) {
    if (ch < 0 || ch > 1 || timestamps == NULL || capacity == 0)
        return 0;

    portMUX_TYPE *mux = (ch == 0) ? &dropMuxA : &dropMuxB;
    portENTER_CRITICAL(mux);
    size_t count = dropTimestampCount[ch];
    if (count > capacity)
        count = capacity;
    size_t oldest = (dropTimestampHead[ch] + DPM_TIMESTAMP_COUNT - count) % DPM_TIMESTAMP_COUNT;
    for (size_t i = 0; i < count; i++)
        timestamps[i] = dropTimestamps[ch][(oldest + i) % DPM_TIMESTAMP_COUNT];
    portEXIT_CRITICAL(mux);
    return count;
}

void resetDropTimestamps_Safe(int ch) {
    if (ch < 0 || ch > 1)
        return;
    portMUX_TYPE *mux = (ch == 0) ? &dropMuxA : &dropMuxB;
    portENTER_CRITICAL(mux);
    dropTimestampHead[ch] = 0;
    dropTimestampCount[ch] = 0;
    portEXIT_CRITICAL(mux);
}

uint32_t getDropSequence_Safe(int ch) {
    if (ch < 0 || ch > 1)
        return 0;
    portMUX_TYPE *mux = (ch == 0) ? &dropMuxA : &dropMuxB;
    portENTER_CRITICAL(mux);
    uint32_t sequence = dropSequence[ch];
    portEXIT_CRITICAL(mux);
    return sequence;
}

unsigned long getLastDropTime_Safe(int ch) {
    if (ch < 0 || ch > 1)
        return 0;
    portMUX_TYPE *mux = (ch == 0) ? &dropMuxA : &dropMuxB;
    portENTER_CRITICAL(mux);
    unsigned long timestamp = infus[ch].lastDropTime;
    portEXIT_CRITICAL(mux);
    return timestamp;
}

long getPulseCount_Safe(int ch) {
    if (ch < 0 || ch > 1)
        return 0;
    portMUX_TYPE *mux = (ch == 0) ? &dropMuxA : &dropMuxB;
    portENTER_CRITICAL(mux);
    long count = (ch == 0) ? pulseCountA : pulseCountB;
    portEXIT_CRITICAL(mux);
    return count;
}

static bool startChecksPass(int ch) {
    if (!infus[ch].isSensorActive ||
        infus[ch].targetDPM < 5 || infus[ch].targetDPM > 150 ||
        infus[ch].dropFactor <= 0 || infus[ch].volumeAwal <= 0.0f)
        return false;

    PumpControlProfile profile;
    if (!isPumpOutputReady(ch, &profile))
        return false;

    float targetFlowMlHr;
    if (!dpmToNominalFlowMlHr(infus[ch].targetDPM, infus[ch].dropFactor,
                              &targetFlowMlHr))
        return false;
    return targetFlowMlHr >= profile.minFlowMlHr &&
           targetFlowMlHr <= profile.maxFlowMlHr;
}

static void selectRemainingAlarm() {
    alarmChannelTrigger = -1;
    for (int i = 0; i < 2; i++) {
        if (infus[i].state == BED_ALARM) {
            alarmChannelTrigger = i;
            break;
        }
    }
}

// Encoder navigation and bed state transitions run on the UI task under dripMutex.
void handleEncoderInput() {
    bool currentBtnState = (digitalRead(PIN_ENC_SW) == LOW);
    if (currentBtnState && !buttonIsPressed) {
        buttonIsPressed = true;
        buttonPressStart = millis();
    } else if (!currentBtnState && buttonIsPressed) {
        unsigned long duration = millis() - buttonPressStart;
        buttonIsPressed = false;
        if (duration >= 1000)
            encButtonLongPressed = true;
        else if (duration >= 40)
            encButtonClicked = true;
    }

    portENTER_CRITICAL(&encoderMux);
    int delta = encCounterDelta;
    encCounterDelta = 0;
    portEXIT_CRITICAL(&encoderMux);
    bool clicked = encButtonClicked;
    bool longPressed = encButtonLongPressed;
    encButtonClicked = false;
    encButtonLongPressed = false;

    if (!xSemaphoreTake(dripMutex, pdMS_TO_TICKS(10)))
        return;

    if (longPressed && currentScreen != SCREEN_STARTUP) {
        currentScreen = currentScreen == SCREEN_SUMMARY ? SCREEN_MAIN_SETTINGS : SCREEN_SUMMARY;
        xSemaphoreGive(dripMutex);
        return;
    }

    switch (currentScreen) {
        case SCREEN_STARTUP:
            break;

        case SCREEN_SUMMARY:
            if (delta != 0)
                selectedChannel = selectedChannel == 0 ? 1 : 0;
            if (clicked)
                currentScreen = selectedChannel == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
            break;

        case SCREEN_DETAIL_A:
        case SCREEN_DETAIL_B: {
            int ch = currentScreen == SCREEN_DETAIL_A ? 0 : 1;
            if (delta != 0 && infus[ch].state == BED_STANDBY) {
                infus[ch].targetDPM += delta;
                if (infus[ch].targetDPM < 5) infus[ch].targetDPM = 5;
                if (infus[ch].targetDPM > 150) infus[ch].targetDPM = 150;
                infus[ch].configurationConfirmed = false;
            }
            if (clicked) {
                bedActionIndex = 0;
                currentScreen = SCREEN_BED_ACTIONS;
            }
            break;
        }

        case SCREEN_BED_ACTIONS: {
            int ch = selectedChannel;
            int actionCount = infus[ch].state == BED_STANDBY ? 2 :
                              infus[ch].state == BED_ALARM ? 3 : 3;
            if (delta != 0) {
                bedActionIndex = (bedActionIndex + delta) % actionCount;
                if (bedActionIndex < 0)
                    bedActionIndex += actionCount;
            }
            if (!clicked)
                break;

            if (infus[ch].state == BED_STANDBY) {
                if (bedActionIndex == 0)
                    currentScreen = SCREEN_START_CONFIRMATION;
                else
                    currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
            } else if (infus[ch].state == BED_RUNNING) {
                if (bedActionIndex == 0) {
                    if (preparePumpOutputForInactiveState(ch)) {
                        infus[ch].state = BED_PAUSED;
                        currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                    }
                } else if (bedActionIndex == 1) {
                    if (preparePumpOutputForInactiveState(ch)) {
                        infus[ch].state = BED_STANDBY;
                        infus[ch].configurationConfirmed = false;
                        currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                    }
                } else {
                    currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                }
            } else if (infus[ch].state == BED_PAUSED) {
                if (bedActionIndex == 0)
                    currentScreen = SCREEN_START_CONFIRMATION;
                else if (bedActionIndex == 1) {
                    if (preparePumpOutputForInactiveState(ch)) {
                        infus[ch].state = BED_STANDBY;
                        infus[ch].configurationConfirmed = false;
                        currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                    }
                } else {
                    currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                }
            } else {
                if (bedActionIndex == 0) {
                    infus[ch].alarmAcknowledged = true;
                } else if (bedActionIndex == 1 && infus[ch].alarmAcknowledged &&
                           infus[ch].alarmRecoveryReady) {
                    currentScreen = SCREEN_ALARM_RESET_CONFIRMATION;
                } else if (bedActionIndex == 2) {
                    currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
                }
            }
            break;
        }

        case SCREEN_START_CONFIRMATION: {
            int ch = selectedChannel;
            if (clicked && (infus[ch].state == BED_STANDBY || infus[ch].state == BED_PAUSED) &&
                startChecksPass(ch)) {
                getDropCount_Safe(ch);
                resetDropTimestamps_Safe(ch);
                infus[ch].configurationConfirmed = true;
                infus[ch].state = BED_RUNNING;
                infus[ch].alarmAcknowledged = false;
                currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
            }
            break;
        }

        case SCREEN_ALARM_RESET_CONFIRMATION: {
            int ch = selectedChannel;
            if (clicked && infus[ch].state == BED_ALARM &&
                infus[ch].alarmAcknowledged &&
                infus[ch].alarmRecoveryReady &&
                preparePumpOutputForInactiveState(ch)) {
                infus[ch].state = BED_STANDBY;
                infus[ch].configurationConfirmed = false;
                infus[ch].alarmAcknowledged = false;
                infus[ch].alarmRecoveryReady = false;
                infus[ch].actualDPM = 0;
                infus[ch].dpmValid = false;
                infus[ch].alarmCode = ALARM_NONE;
                resetDropTimestamps_Safe(ch);
                selectRemainingAlarm();
                currentScreen = ch == 0 ? SCREEN_DETAIL_A : SCREEN_DETAIL_B;
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
            if (delta != 0 && infus[selectedChannel].state == BED_STANDBY) {
                infus[selectedChannel].targetDPM += delta;
                if (infus[selectedChannel].targetDPM < 5) infus[selectedChannel].targetDPM = 5;
                if (infus[selectedChannel].targetDPM > 150) infus[selectedChannel].targetDPM = 150;
                infus[selectedChannel].configurationConfirmed = false;
            }
            if (clicked)
                currentScreen = SCREEN_CONFIRMATION;
            break;

        case SCREEN_VOLUME_CONFIG:
            if (delta != 0 && infus[selectedChannel].state == BED_STANDBY) {
                infus[selectedChannel].volumeAwal += (delta * 50);
                if (infus[selectedChannel].volumeAwal < 50) infus[selectedChannel].volumeAwal = 50;
                if (infus[selectedChannel].volumeAwal > 2000) infus[selectedChannel].volumeAwal = 2000;
                infus[selectedChannel].sisaVolume = infus[selectedChannel].volumeAwal;
                infus[selectedChannel].configurationConfirmed = false;
            }
            if (clicked)
                currentScreen = SCREEN_CONFIRMATION;
            break;

        case SCREEN_NETWORK_STATUS:
        case SCREEN_SENSOR_DIAGNOSTICS:
        case SCREEN_ALARM_HISTORY:
        case SCREEN_SYSTEM_INFO:
            if (clicked)
                currentScreen = SCREEN_MAIN_SETTINGS;
            break;

        case SCREEN_ALARM_WARNING:
            if (clicked) {
                if (alarmChannelTrigger >= 0 && alarmChannelTrigger < 2) {
                    infus[alarmChannelTrigger].alarmAcknowledged = true;
                    selectedChannel = alarmChannelTrigger;
                }
                currentScreen = SCREEN_SUMMARY;
            }
            break;

        case SCREEN_CONFIRMATION:
            if (clicked)
                currentScreen = SCREEN_MAIN_SETTINGS;
            break;
    }

    xSemaphoreGive(dripMutex);
}
