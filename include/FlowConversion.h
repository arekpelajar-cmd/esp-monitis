#ifndef FLOW_CONVERSION_H
#define FLOW_CONVERSION_H

inline bool dpmToNominalFlowMlHr(int dpm, int dropFactor, float *flowMlHr) {
  if (flowMlHr == 0 || dpm < 0 || dropFactor <= 0)
    return false;
  *flowMlHr = (float)dpm * 60.0f / (float)dropFactor;
  return true;
}

#endif
