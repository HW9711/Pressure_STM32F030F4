#ifndef __WEIGHT_CALIBRATION_H__
#define __WEIGHT_CALIBRATION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* CS1237 原始重量换算基础系数 */
#define CS1237_SCALE_BASE            14751U
#define CS1237_SCALE_FACTOR          CS1237_SCALE_BASE
#define CS1237_SEGMENT_CAL_ENABLE    1U
#define CS1237_CAL_POINT_COUNT       6U
#define CS1237_CAL0_REAL_X10         0U
#define CS1237_CAL0_RAW              (-5446)
#define CS1237_CAL1_REAL_X10         218U
#define CS1237_CAL1_RAW              13082
#define CS1237_CAL2_REAL_X10         1218U
#define CS1237_CAL2_RAW              95588
#define CS1237_CAL3_REAL_X10         2218U
#define CS1237_CAL3_RAW              178174
#define CS1237_CAL4_REAL_X10         5218U
#define CS1237_CAL4_RAW              419128
#define CS1237_CAL5_REAL_X10         10218U
#define CS1237_CAL5_RAW              817285

/* 按当前工程的分段标定点，把 CS1237 原始值换算成 0.1g */
uint32_t WeightCalibration_ApplySegmentCalibrationX10(int32_t raw_value);

#ifdef __cplusplus
}
#endif

#endif /* __WEIGHT_CALIBRATION_H__ */