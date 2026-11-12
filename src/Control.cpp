#include "Control.h"
#include "Config.h"
#include "DpmEstimator.h"
#include "FlowConversion.h"
#include "PumpOutput.h"
#include "Sensor.h"
#include <math.h>

volatile bool buttonPressed = false;

void IRAM_ATTR encoderISR() {
  buttonPressed = true;
}

unsigned long alarmEnterTime = 0;
static unsigned long monitorLastPulseTime[2] = {};
static unsigned long lastPumpUpdateTime[2] = {};
static uint32_t lastSeenDropSequence[2] = {};
static uint32_t lastControlDropSequence[2] = {};
static uint32_t alarmStartDropSequence[2] = {};
static bool samplingActive[2] = {};
static bool controllerHasUpdate[2] = {};
static BedState controlledState[2] = {BED_STANDBY, BED_STANDBY};

static void resetPumpController(int channel, unsigned long now) {
  infus[channel].integral = 0.0f;
  infus[channel].lastError = 0.0f;
  infus[channel].pumpCommandFlowRateMlHr = 0.0f;
  infus[channel].pumpCommandKnown = false;
  infus[channel].pumpCommandActive = false;
  lastControlDropSequence[channel] = getDropSequence_Safe(channel);
  lastPumpUpdateTime[channel] = now;
  controllerHasUpdate[channel] = false;
}

static float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum)
    return minimum;
  if (value > maximum)
    return maximum;
  return value;
}

static float calculatePumpRequest(int channel,
                                  const PumpControlProfile &profile,
                                  float dtSeconds) {
  float error = infus[channel].targetFlowRateMlHr -
                infus[channel].measuredFlowRateMlHr;
  if (fabsf(error) <= profile.deadbandMlHr)
    error = 0.0f;

  if (dtSeconds <= 0.0f)
    dtSeconds = 0.1f;

  const float candidateIntegral = infus[channel].integral + error * dtSeconds;
  const float derivative =
      (error - infus[channel].lastError) / dtSeconds;
  const float target = infus[channel].targetFlowRateMlHr;
  const float previousRequest =
      infus[channel].pumpCommandKnown
          ? infus[channel].pumpCommandFlowRateMlHr
          : 0.0f;
  const float maxStep = profile.maxSlewMlHrPerSec * dtSeconds;

  float raw = target + profile.kp * error +
              profile.ki * candidateIntegral + profile.kd * derivative;
  float bounded = clampFloat(raw, profile.minFlowMlHr, profile.maxFlowMlHr);
  float lower = fmaxf(profile.minFlowMlHr, previousRequest - maxStep);
  float upper = fminf(profile.maxFlowMlHr, previousRequest + maxStep);
  if (lower > upper)
    lower = upper;
  float slewed = clampFloat(bounded, lower, upper);

  const bool saturatedAgainstError =
      (raw > profile.maxFlowMlHr && error > 0.0f) ||
      (raw < profile.minFlowMlHr && error < 0.0f) ||
      (bounded > slewed && error > 0.0f) ||
      (bounded < slewed && error < 0.0f);

  if (!saturatedAgainstError) {
    infus[channel].integral = candidateIntegral;
  } else {
    raw = target + profile.kp * error +
          profile.ki * infus[channel].integral +
          profile.kd * derivative;
    bounded = clampFloat(raw, profile.minFlowMlHr, profile.maxFlowMlHr);
    slewed = clampFloat(bounded, lower, upper);
  }

  infus[channel].lastError = error;
  return slewed;
}

static void updateMedicalFields(int channel) {
  float nominalTargetFlow = 0.0f;
  if (!dpmToNominalFlowMlHr(infus[channel].targetDPM,
                            infus[channel].dropFactor,
                            &nominalTargetFlow) ||
      infus[channel].targetDPM == 0) {
    infus[channel].targetFlowRateMlHr = 0.0f;
  } else
    infus[channel].targetFlowRateMlHr = nominalTargetFlow;

  float nominalMeasuredFlow = 0.0f;
  if (!infus[channel].dpmValid ||
      !dpmToNominalFlowMlHr(infus[channel].actualDPM,
                            infus[channel].dropFactor,
                            &nominalMeasuredFlow)) {
    infus[channel].measuredFlowRateMlHr = 0.0f;
  } else
    infus[channel].measuredFlowRateMlHr = nominalMeasuredFlow;

  if (infus[channel].dpmValid && infus[channel].actualDPM > 0 &&
      infus[channel].dropFactor > 0 && infus[channel].sisaVolume > 0.0f) {
    infus[channel].etaMinutes =
        (long)((infus[channel].sisaVolume * (float)infus[channel].dropFactor) /
               (float)infus[channel].actualDPM);
  } else {
    infus[channel].etaMinutes = 0L;
  }

  if (infus[channel].state != BED_ALARM)
    infus[channel].alarmCode = ALARM_NONE;
}

static void enterPumpFailureAlarm(int channel, unsigned long now) {
  infus[channel].state = BED_ALARM;
  infus[channel].configurationConfirmed = false;
  infus[channel].alarmAcknowledged = false;
  infus[channel].alarmRecoveryReady = false;
  infus[channel].alarmCode = ALARM_PUMP_OUTPUT_FAILURE;
  infus[channel].actualDPM = 0;
  infus[channel].dpmValid = false;
  infus[channel].dpmStale = false;
  infus[channel].pumpCommandActive = false;
  alarmStartDropSequence[channel] = getDropSequence_Safe(channel);
  resetDropTimestamps_Safe(channel);
  samplingActive[channel] = false;
  alarmChannelTrigger = channel;
  alarmEnterTime = now;
  currentScreen = SCREEN_ALARM_WARNING;
}

bool preparePumpOutputForInactiveState(int channel) {
  if (channel < 0 || channel >= 2)
    return false;
  if (!disablePumpOutput(channel)) {
    enterPumpFailureAlarm(channel, millis());
    return false;
  }
  infus[channel].pumpCommandFlowRateMlHr = 0.0f;
  infus[channel].pumpCommandKnown = false;
  infus[channel].pumpCommandActive = false;
  infus[channel].integral = 0.0f;
  infus[channel].lastError = 0.0f;
  return true;
}

void initControl() {
  const unsigned long now = millis();
  for (int i = 0; i < 2; i++) {
    infus[i].state = BED_STANDBY;
    infus[i].configurationConfirmed = false;
    infus[i].dpmValid = false;
    infus[i].dpmStale = false;
    infus[i].alarmAcknowledged = false;
    infus[i].alarmRecoveryReady = false;
    infus[i].targetFlowRateMlHr = 0.0f;
    infus[i].measuredFlowRateMlHr = 0.0f;
    infus[i].etaMinutes = 0L;
    infus[i].alarmCode = ALARM_NONE;
    resetPumpController(i, now);
    monitorLastPulseTime[i] = now;
    lastSeenDropSequence[i] = getDropSequence_Safe(i);
    samplingActive[i] = false;
    controlledState[i] = BED_STANDBY;
    if (!disablePumpOutput(i)) {
      enterPumpFailureAlarm(i, now);
      controlledState[i] = BED_ALARM;
    }
  }
}

void taskControlCode(void *pvParameters) {
  (void)pvParameters;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(100);

  for (;;) {
    const unsigned long now = millis();
    if (!xSemaphoreTake(dripMutex, pdMS_TO_TICKS(20))) {
      vTaskDelayUntil(&xLastWakeTime, xFrequency);
      continue;
    }

    for (int i = 0; i < 2; i++) {
      BedState state = infus[i].state;
      if (state == BED_RUNNING &&
          (!infus[i].configurationConfirmed || !infus[i].isSensorActive ||
           !isPumpOutputReady(i))) {
        infus[i].state = BED_STANDBY;
        infus[i].configurationConfirmed = false;
        state = BED_STANDBY;
      }

      const uint32_t sequence = getDropSequence_Safe(i);
      const int drops = getDropCount_Safe(i);
      const bool running = state == BED_RUNNING && infus[i].isSensorActive;
      const bool alarm = state == BED_ALARM && infus[i].isSensorActive;

      // Only real, debounced sensor pulses affect volume; estimates never do.
      if ((running || alarm) && drops > 0 && infus[i].dropFactor > 0) {
        infus[i].sisaVolume -=
            (float)drops / (float)infus[i].dropFactor;
        if (infus[i].sisaVolume < 0.0f)
          infus[i].sisaVolume = 0.0f;
      }

      if (!running && !alarm) {
        if (samplingActive[i])
          resetDropTimestamps_Safe(i);
        samplingActive[i] = false;
        infus[i].actualDPM = 0;
        infus[i].dpmValid = false;
        infus[i].dpmStale = false;
        infus[i].alarmRecoveryReady = false;
        if (state != BED_ALARM)
          infus[i].alarmAcknowledged = false;
      } else {
        if (!samplingActive[i]) {
          resetDropTimestamps_Safe(i);
          samplingActive[i] = true;
          if (running)
            monitorLastPulseTime[i] = now;
          lastSeenDropSequence[i] = sequence;
          if (alarm)
            alarmStartDropSequence[i] = sequence;
        }

        if (sequence != lastSeenDropSequence[i]) {
          lastSeenDropSequence[i] = sequence;
          if (running)
            monitorLastPulseTime[i] = now;
        }

        uint32_t timestamps[DPM_TIMESTAMP_COUNT];
        const size_t timestampCount =
            getDropTimestamps_Safe(i, timestamps, DPM_TIMESTAMP_COUNT);
        const DpmEstimate estimate =
            estimateDpmFromTimestamps(timestamps, timestampCount, now,
                                      DPM_STALE_TIMEOUT_MS);
        infus[i].dpmValid = estimate.valid;
        const bool alarmHasNoNewPulse =
            alarm && sequence == alarmStartDropSequence[i];
        infus[i].dpmStale =
            estimate.stale ||
            (alarmHasNoNewPulse && infus[i].dpmStale);
        infus[i].actualDPM = estimate.valid ? estimate.dpm : 0;

        if (running &&
            (uint32_t)(now - monitorLastPulseTime[i]) >
                NO_FLOW_ALARM_TIMEOUT_MS) {
          infus[i].state = BED_ALARM;
          infus[i].alarmAcknowledged = false;
          infus[i].alarmRecoveryReady = false;
          infus[i].alarmCode = ALARM_FLOW_NOT_DETECTED;
          alarmChannelTrigger = i;
          alarmEnterTime = now;
          currentScreen = SCREEN_ALARM_WARNING;
          alarmStartDropSequence[i] = sequence;
          resetDropTimestamps_Safe(i);
          infus[i].actualDPM = 0;
          infus[i].dpmValid = false;
        } else if (alarm) {
          infus[i].alarmRecoveryReady =
              (uint32_t)(sequence - alarmStartDropSequence[i]) >=
                  ALARM_RECOVERY_PULSE_COUNT &&
              estimate.valid;
        }
      }

      updateMedicalFields(i);
    }

    for (int i = 0; i < 2; i++) {
      BedState state = infus[i].state;
      if (state != controlledState[i]) {
        resetPumpController(i, now);
        if (!disablePumpOutput(i)) {
          enterPumpFailureAlarm(i, now);
          state = BED_ALARM;
        }
        controlledState[i] = state;
      }

      if (state != BED_RUNNING) {
        infus[i].integral = 0.0f;
        infus[i].pumpCommandActive = false;
        continue;
      }

      PumpControlProfile profile;
      if (!isPumpOutputReady(i, &profile)) {
        infus[i].state = BED_STANDBY;
        infus[i].configurationConfirmed = false;
        continue;
      }

      const uint32_t sequence = getDropSequence_Safe(i);
      if (!infus[i].dpmValid || sequence == lastControlDropSequence[i])
        continue;

      float dtSeconds =
          (float)(uint32_t)(now - lastPumpUpdateTime[i]) / 1000.0f;
      if (dtSeconds <= 0.0f)
        dtSeconds = 0.1f;
      float flowRequest;
      if (controllerHasUpdate[i]) {
        flowRequest = calculatePumpRequest(i, profile, dtSeconds);
      } else {
        const float maxStep = profile.maxSlewMlHrPerSec * dtSeconds;
        const float lower =
            fmaxf(profile.minFlowMlHr,
                  infus[i].pumpCommandFlowRateMlHr - maxStep);
        const float upper =
            fminf(profile.maxFlowMlHr,
                  infus[i].pumpCommandFlowRateMlHr + maxStep);
        flowRequest = clampFloat(infus[i].targetFlowRateMlHr,
                                 lower, upper);
        infus[i].lastError = infus[i].targetFlowRateMlHr -
                             infus[i].measuredFlowRateMlHr;
      }
      if (!setPumpFlowRateMlHr(i, flowRequest)) {
        disablePumpOutput(i);
        enterPumpFailureAlarm(i, now);
        controlledState[i] = BED_ALARM;
        resetPumpController(i, now);
        continue;
      }

      infus[i].pumpCommandFlowRateMlHr = flowRequest;
      infus[i].pumpCommandKnown = true;
      infus[i].pumpCommandActive = true;
      lastControlDropSequence[i] = sequence;
      lastPumpUpdateTime[i] = now;
      controllerHasUpdate[i] = true;
    }

    xSemaphoreGive(dripMutex);
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
