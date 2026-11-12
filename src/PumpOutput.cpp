#include "PumpOutput.h"
#include "Config.h"
#include <math.h>

static PumpOutputDriver *outputDriver = 0;

void registerPumpOutputDriver(PumpOutputDriver *driver) {
  outputDriver = driver;
}

bool isPumpOutputReady(int channel, PumpControlProfile *profile) {
#if PUMP_OUTPUT_ENABLED
  if (channel < 0 || channel >= 2 || outputDriver == 0)
    return false;

  PumpControlProfile candidate;
  if (!outputDriver->getControlProfile(channel, candidate) ||
      !isfinite(candidate.minFlowMlHr) ||
      !isfinite(candidate.maxFlowMlHr) ||
      !isfinite(candidate.kp) ||
      !isfinite(candidate.ki) ||
      !isfinite(candidate.kd) ||
      !isfinite(candidate.deadbandMlHr) ||
      !isfinite(candidate.maxSlewMlHrPerSec) ||
      candidate.minFlowMlHr < 0.0f ||
      candidate.maxFlowMlHr <= candidate.minFlowMlHr ||
      candidate.kp < 0.0f || candidate.ki < 0.0f || candidate.kd < 0.0f ||
      candidate.deadbandMlHr < 0.0f ||
      candidate.maxSlewMlHrPerSec <= 0.0f)
    return false;

  if (profile != 0)
    *profile = candidate;
  return true;
#else
  (void)channel;
  (void)profile;
  return false;
#endif
}

bool setPumpFlowRateMlHr(int channel, float flowMlHr) {
#if PUMP_OUTPUT_ENABLED
  if (!isfinite(flowMlHr) || !isPumpOutputReady(channel) ||
      outputDriver == 0)
    return false;
  PumpControlProfile profile;
  if (!isPumpOutputReady(channel, &profile) ||
      flowMlHr < profile.minFlowMlHr || flowMlHr > profile.maxFlowMlHr)
    return false;
  return outputDriver->setFlowRateMlHr(channel, flowMlHr);
#else
  (void)channel;
  (void)flowMlHr;
  return false;
#endif
}

bool disablePumpOutput(int channel) {
#if PUMP_OUTPUT_ENABLED
  if (channel < 0 || channel >= 2 || outputDriver == 0)
    return false;
  return outputDriver->disableOutput(channel);
#else
  (void)channel;
  return true;
#endif
}
