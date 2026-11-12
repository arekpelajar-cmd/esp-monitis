#include <unity.h>
#include "DpmEstimator.h"
#include "FlowConversion.h"

void setUp(void) {}
void tearDown(void) {}

static void test_five_timestamps_over_twelve_seconds() {
  const uint32_t timestamps[] = {0, 3000, 6000, 9000, 12000};
  const DpmEstimate estimate =
      estimateDpmFromTimestamps(timestamps, 5, 12000, 15000);
  TEST_ASSERT_TRUE(estimate.valid);
  TEST_ASSERT_FALSE(estimate.stale);
  TEST_ASSERT_EQUAL_INT(20, estimate.dpm);
}

static void test_estimate_is_not_rounded_to_sixty_dpm_steps() {
  const uint32_t timestamps[] = {0, 2500, 5000};
  const DpmEstimate estimate =
      estimateDpmFromTimestamps(timestamps, 3, 5000, 15000);
  TEST_ASSERT_TRUE(estimate.valid);
  TEST_ASSERT_EQUAL_INT(24, estimate.dpm);
}

static void test_insufficient_samples_are_invalid() {
  const uint32_t timestamps[] = {1000};
  const DpmEstimate estimate =
      estimateDpmFromTimestamps(timestamps, 1, 1000, 15000);
  TEST_ASSERT_FALSE(estimate.valid);
  TEST_ASSERT_FALSE(estimate.stale);
}

static void test_old_samples_are_marked_stale() {
  const uint32_t timestamps[] = {1000, 4000};
  const DpmEstimate estimate =
      estimateDpmFromTimestamps(timestamps, 2, 20000, 15000);
  TEST_ASSERT_FALSE(estimate.valid);
  TEST_ASSERT_TRUE(estimate.stale);
}

static void test_old_single_sample_is_also_stale() {
  const uint32_t timestamps[] = {1000};
  const DpmEstimate estimate =
      estimateDpmFromTimestamps(timestamps, 1, 20000, 15000);
  TEST_ASSERT_FALSE(estimate.valid);
  TEST_ASSERT_TRUE(estimate.stale);
}

static void test_millis_rollover_uses_unsigned_elapsed_time() {
  const uint32_t timestamps[] = {
      0xFFFFFF00UL, 0x00000AB8UL, 0x00001670UL};
  const DpmEstimate estimate =
      estimateDpmFromTimestamps(timestamps, 3, 0x00001670UL, 15000);
  TEST_ASSERT_TRUE(estimate.valid);
  TEST_ASSERT_EQUAL_INT(20, estimate.dpm);
}

static void test_nominal_flow_conversion_uses_drop_factor() {
  float flowMlHr = 0.0f;
  TEST_ASSERT_TRUE(dpmToNominalFlowMlHr(20, 20, &flowMlHr));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, flowMlHr);
  TEST_ASSERT_FALSE(dpmToNominalFlowMlHr(20, 0, &flowMlHr));
}

#if defined(ARDUINO)
void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_five_timestamps_over_twelve_seconds);
  RUN_TEST(test_estimate_is_not_rounded_to_sixty_dpm_steps);
  RUN_TEST(test_insufficient_samples_are_invalid);
  RUN_TEST(test_old_samples_are_marked_stale);
  RUN_TEST(test_old_single_sample_is_also_stale);
  RUN_TEST(test_millis_rollover_uses_unsigned_elapsed_time);
  RUN_TEST(test_nominal_flow_conversion_uses_drop_factor);
  UNITY_END();
}

void loop() {}
#else
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_five_timestamps_over_twelve_seconds);
  RUN_TEST(test_estimate_is_not_rounded_to_sixty_dpm_steps);
  RUN_TEST(test_insufficient_samples_are_invalid);
  RUN_TEST(test_old_samples_are_marked_stale);
  RUN_TEST(test_old_single_sample_is_also_stale);
  RUN_TEST(test_millis_rollover_uses_unsigned_elapsed_time);
  RUN_TEST(test_nominal_flow_conversion_uses_drop_factor);
  return UNITY_END();
}
#endif
