#include "weight_calibration.h"

typedef struct {
  int32_t delta_raw;
  uint32_t weight_x10;
} WeightCalibrationPoint_t;

static const WeightCalibrationPoint_t g_common_curve[WEIGHT_CALIBRATION_POINT_COUNT] = {
    {WEIGHT_CALIBRATION_RAW_EMPTY, 0U},
    {WEIGHT_CALIBRATION_RAW_21P8G, 218U},
    {WEIGHT_CALIBRATION_RAW_26P8G, 268U},
    {WEIGHT_CALIBRATION_RAW_41P8G, 418U},
    {WEIGHT_CALIBRATION_RAW_71P8G, 718U},
    {WEIGHT_CALIBRATION_RAW_121P8G, 1218U},
    {WEIGHT_CALIBRATION_RAW_221P8G, 2218U},
    {WEIGHT_CALIBRATION_RAW_521P8G, 5218U},
    {WEIGHT_CALIBRATION_RAW_1021P8G, 10218U}
};

static uint32_t WeightCalibration_InterpolateX10(int32_t delta_raw,
                                                  const WeightCalibrationPoint_t *lo,
                                                  const WeightCalibrationPoint_t *hi)
{
  int64_t raw_span = (int64_t)hi->delta_raw - (int64_t)lo->delta_raw;
  int64_t weight_span = (int64_t)hi->weight_x10 - (int64_t)lo->weight_x10;
  int64_t numerator;
  int64_t value_x10;

  if (raw_span <= 0LL) {
    return lo->weight_x10;
  }

  numerator = ((int64_t)delta_raw - (int64_t)lo->delta_raw) * weight_span;
  value_x10 = (int64_t)lo->weight_x10 + ((numerator + (raw_span / 2LL)) / raw_span);
  if (value_x10 <= 0LL) {
    return 0U;
  }
  if (value_x10 > 0xFFFFFFFFLL) {
    return 0xFFFFFFFFUL;
  }
  return (uint32_t)value_x10;
}

uint32_t WeightCalibration_ApplyFromEmptyX10(int32_t delta_from_empty_raw)
{
  uint8_t i;

  if (delta_from_empty_raw <= 0) {
    return 0U;
  }

  for (i = 0U; i < (WEIGHT_CALIBRATION_POINT_COUNT - 1U); i++) {
    if (delta_from_empty_raw <= g_common_curve[i + 1U].delta_raw) {
      return WeightCalibration_InterpolateX10(delta_from_empty_raw,
                                               &g_common_curve[i],
                                               &g_common_curve[i + 1U]);
    }
  }

  return WeightCalibration_InterpolateX10(
      delta_from_empty_raw,
      &g_common_curve[WEIGHT_CALIBRATION_POINT_COUNT - 2U],
      &g_common_curve[WEIGHT_CALIBRATION_POINT_COUNT - 1U]);
}

uint32_t WeightCalibration_ApplySessionX10(int32_t current_from_empty_raw,
                                            int32_t session_zero_from_empty_raw)
{
  uint32_t current_x10 = WeightCalibration_ApplyFromEmptyX10(current_from_empty_raw);
  uint32_t session_zero_x10 = WeightCalibration_ApplyFromEmptyX10(session_zero_from_empty_raw);

  if (current_x10 <= session_zero_x10) {
    return 0U;
  }
  return current_x10 - session_zero_x10;
}
