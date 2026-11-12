#ifndef PUMP_OUTPUT_H
#define PUMP_OUTPUT_H

struct PumpControlProfile {
  float minFlowMlHr;
  float maxFlowMlHr;
  float kp;
  float ki;
  float kd;
  float deadbandMlHr;
  float maxSlewMlHrPerSec;
};

// A hardware backend must map calibrated flow requests to driver-native
// commands. These methods run under the firmware state mutex and must not block.
class PumpOutputDriver {
public:
  virtual ~PumpOutputDriver() {}
  virtual bool getControlProfile(int channel,
                                 PumpControlProfile &profile) const = 0;
  virtual bool setFlowRateMlHr(int channel, float flowMlHr) = 0;
  virtual bool disableOutput(int channel) = 0;
};

void registerPumpOutputDriver(PumpOutputDriver *driver);
bool isPumpOutputReady(int channel, PumpControlProfile *profile = 0);
bool setPumpFlowRateMlHr(int channel, float flowMlHr);
bool disablePumpOutput(int channel);

#endif
