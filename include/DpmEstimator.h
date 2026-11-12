#ifndef DPM_ESTIMATOR_H
#define DPM_ESTIMATOR_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

struct DpmEstimate {
  bool valid;
  bool stale;
  int dpm;
};

inline DpmEstimate estimateDpmFromTimestamps(const uint32_t *timestamps,
                                             size_t count,
                                             uint32_t now,
                                             uint32_t staleTimeoutMs) {
  DpmEstimate result = {false, false, 0};
  if (timestamps == NULL || count == 0)
    return result;

  const uint32_t ageMs = now - timestamps[count - 1];
  if (ageMs > staleTimeoutMs) {
    result.stale = true;
    return result;
  }
  if (count < 2)
    return result;

  const uint32_t elapsedMs = timestamps[count - 1] - timestamps[0];
  if (elapsedMs == 0)
    return result;

  const size_t intervalCount = count - 1;
  result.dpm = (int)lroundf((float)intervalCount * 60000.0f /
                            (float)elapsedMs);
  result.valid = true;
  return result;
}

#endif
