#include "weight_calibration.h"
#include "cs1237.h"

/* 两点之间做线性插值，返回 0.1g 单位的换算结果 */
static uint32_t WeightCalibration_InterpolateSegmentX10(int32_t raw_value,
                                                        int32_t raw_lo,
                                                        int32_t raw_hi,
                                                        uint32_t real_lo_x10,
                                                        uint32_t real_hi_x10)
{
  int64_t delta_raw = (int64_t)raw_hi - (int64_t)raw_lo;
  int64_t delta_real = (int64_t)real_hi_x10 - (int64_t)real_lo_x10;
  int64_t numerator;
  int64_t corrected_x10;

  if (delta_raw == 0LL) {
    return real_lo_x10;
  }

  numerator = ((int64_t)raw_value - (int64_t)raw_lo) * delta_real;

  if (numerator >= 0) {
    corrected_x10 = (int64_t)real_lo_x10 + ((numerator + (delta_raw / 2LL)) / delta_raw);
  } else {
    corrected_x10 = (int64_t)real_lo_x10 + ((numerator - (delta_raw / 2LL)) / delta_raw);
  }

  if (corrected_x10 < 0LL) {
    return 0U;
  }

  return (uint32_t)corrected_x10;
}

/* 将原始码值映射到最接近的分段区间，并输出换算后的重量 */
uint32_t WeightCalibration_ApplySegmentCalibrationX10(int32_t raw_value)
{
#if (CS1237_SEGMENT_CAL_ENABLE != 0U)
  static const int32_t raw_points[CS1237_CAL_POINT_COUNT] = {
      CS1237_CAL0_RAW,
      CS1237_CAL1_RAW,
      CS1237_CAL2_RAW,
      CS1237_CAL3_RAW,
      CS1237_CAL4_RAW,
      CS1237_CAL5_RAW
  };
  static const uint32_t real_points[CS1237_CAL_POINT_COUNT] = {
      CS1237_CAL0_REAL_X10,
      CS1237_CAL1_REAL_X10,
      CS1237_CAL2_REAL_X10,
      CS1237_CAL3_REAL_X10,
      CS1237_CAL4_REAL_X10,
      CS1237_CAL5_REAL_X10
  };
  uint8_t i;
  uint8_t best_idx = 0U;
  uint32_t best_distance = 0xFFFFFFFFU;

  /* 相邻标定点重复时，直接退回默认测量值，避免除零 */
  if ((raw_points[0] == raw_points[1]) ||
      (raw_points[1] == raw_points[2]) ||
      (raw_points[2] == raw_points[3]) ||
      (raw_points[3] == raw_points[4]) ||
      (raw_points[4] == raw_points[5])) {
    return CS1237_GetMeasurementX10();
  }

  /* 先找当前值落在哪个区间 */
  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {
    int32_t raw_a = raw_points[i];
    int32_t raw_b = raw_points[i + 1U];
    int32_t range_min = (raw_a < raw_b) ? raw_a : raw_b;
    int32_t range_max = (raw_a > raw_b) ? raw_a : raw_b;

    if ((raw_value >= range_min) && (raw_value <= range_max)) {
      return WeightCalibration_InterpolateSegmentX10(raw_value,
                                                     raw_a,
                                                     raw_b,
                                                     real_points[i],
                                                     real_points[i + 1U]);
    }
  }

  /* 不在区间内时，选择最近的一段做插值 */
  for (i = 0U; i < (CS1237_CAL_POINT_COUNT - 1U); i++) {
    int32_t raw_a = raw_points[i];
    int32_t raw_b = raw_points[i + 1U];
    uint32_t distance_a = (raw_value > raw_a) ? (uint32_t)(raw_value - raw_a) : (uint32_t)(raw_a - raw_value);
    uint32_t distance_b = (raw_value > raw_b) ? (uint32_t)(raw_value - raw_b) : (uint32_t)(raw_b - raw_value);
    uint32_t segment_distance = (distance_a < distance_b) ? distance_a : distance_b;

    if (segment_distance < best_distance) {
      best_distance = segment_distance;
      best_idx = i;
    }
  }

  return WeightCalibration_InterpolateSegmentX10(raw_value,
                                                 raw_points[best_idx],
                                                 raw_points[best_idx + 1U],
                                                 real_points[best_idx],
                                                 real_points[best_idx + 1U]);
#else
  return CS1237_GetMeasurementX10();
#endif
}