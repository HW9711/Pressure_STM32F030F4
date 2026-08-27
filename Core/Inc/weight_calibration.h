#ifndef __WEIGHT_CALIBRATION_H__
#define __WEIGHT_CALIBRATION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/*
 * Source: pressure-sensor-increment-comparison.csv, 11 sensors.
 * Each sensor is rebased at its own true-empty point. For every total load,
 * one maximum and one minimum are removed before averaging the raw increment.
 */
#define WEIGHT_CALIBRATION_POINT_COUNT       9U

#define WEIGHT_CALIBRATION_RAW_EMPTY         0
#define WEIGHT_CALIBRATION_RAW_21P8G         18938
#define WEIGHT_CALIBRATION_RAW_26P8G         23184
#define WEIGHT_CALIBRATION_RAW_41P8G         35763
#define WEIGHT_CALIBRATION_RAW_71P8G         61260
#define WEIGHT_CALIBRATION_RAW_121P8G        101540
#define WEIGHT_CALIBRATION_RAW_221P8G        182078
#define WEIGHT_CALIBRATION_RAW_521P8G        422842
#define WEIGHT_CALIBRATION_RAW_1021P8G       804660

uint32_t WeightCalibration_ApplyFromEmptyX10(int32_t delta_from_empty_raw);
uint32_t WeightCalibration_ApplySessionX10(int32_t current_from_empty_raw,
                                            int32_t session_zero_from_empty_raw);

#ifdef __cplusplus
}
#endif

#endif /* __WEIGHT_CALIBRATION_H__ */
