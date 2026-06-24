#ifndef __WEIGHT_CALIBRATION_H__
#define __WEIGHT_CALIBRATION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct {
    int32_t raw_value;
    uint32_t real_x10;
} WeightCalibrationPoint_t;

/* CS1237 原始重量换算基础系数；运行时分段标定有效时仅作为异常回退路径。 */
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

/* 上位机在线标定最多保存 8 个点：21.8g 托盘 + 0/5/20/50/100/200/500/1000g 砝码。 */
#define WEIGHT_CALIBRATION_RUNTIME_ENABLE        1U
#define WEIGHT_CALIBRATION_MAX_POINTS            8U
#define WEIGHT_CALIBRATION_FLASH_ADDR            0x08003800U
#define WEIGHT_CALIBRATION_FLASH_MAGIC           0x5743414CUL
#define WEIGHT_CALIBRATION_FLASH_VERSION         1U

/* 上电时从 Flash 载入上位机写入的标定表；失败时继续使用上面的编译期默认表。 */
void WeightCalibration_LoadRuntimeFromFlash(void);

/* 保存上位机下发的标定表，保存成功后立即切到新表运行。 */
uint8_t WeightCalibration_SaveRuntimeTable(const WeightCalibrationPoint_t *points, uint8_t count);

/* 清除运行时标定表，后续重新回到编译期默认表。 */
uint8_t WeightCalibration_ClearRuntimeTable(void);

/* 查询当前是否正在使用上位机写入的运行时标定表。 */
uint8_t WeightCalibration_HasRuntimeTable(void);
uint8_t WeightCalibration_GetRuntimePointCount(void);

/* 按当前工程的分段标定点，把 CS1237 原始值换算成 0.1g。 */
uint32_t WeightCalibration_ApplySegmentCalibrationX10(int32_t raw_value);

#ifdef __cplusplus
}
#endif

#endif /* __WEIGHT_CALIBRATION_H__ */
