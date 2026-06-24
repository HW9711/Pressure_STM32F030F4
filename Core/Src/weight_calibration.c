#include "weight_calibration.h"
#include "cs1237.h"

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t point_count;
  WeightCalibrationPoint_t points[WEIGHT_CALIBRATION_MAX_POINTS];
  uint32_t check;
} WeightCalibrationFlash_t;

static WeightCalibrationPoint_t g_runtime_points[WEIGHT_CALIBRATION_MAX_POINTS];
static uint8_t g_runtime_point_count = 0U;

static uint32_t WeightCalibration_MixCheck(uint32_t check, uint32_t value)
{
  return check ^ (value + 0x9E3779B9UL + (check << 6) + (check >> 2));
}

static uint32_t WeightCalibration_MakeCheck(const WeightCalibrationFlash_t *store_data)
{
  uint32_t check = 0xC1237A5AUL;
  uint8_t i;

  if (store_data == NULL) {
    return 0U;
  }

  check = WeightCalibration_MixCheck(check, store_data->magic);
  check = WeightCalibration_MixCheck(check, store_data->version);
  check = WeightCalibration_MixCheck(check, store_data->point_count);

  for (i = 0U; i < store_data->point_count; i++) {
    check = WeightCalibration_MixCheck(check, (uint32_t)store_data->points[i].raw_value);
    check = WeightCalibration_MixCheck(check, store_data->points[i].real_x10);
  }

  return check;
}

static uint8_t WeightCalibration_IsValidTable(const WeightCalibrationPoint_t *points, uint8_t count)
{
  uint8_t i;

  if ((points == NULL) || (count < 2U) || (count > WEIGHT_CALIBRATION_MAX_POINTS)) {
    return 0U;
  }

  for (i = 0U; i < (count - 1U); i++) {
    if (points[i].raw_value == points[i + 1U].raw_value) {
      return 0U;
    }
    if (points[i].real_x10 >= points[i + 1U].real_x10) {
      return 0U;
    }
  }

  return 1U;
}

static void WeightCalibration_CopyRuntimeTable(const WeightCalibrationPoint_t *points, uint8_t count)
{
  uint8_t i;

  for (i = 0U; i < count; i++) {
    g_runtime_points[i] = points[i];
  }
  g_runtime_point_count = count;
}

/* 两点之间做线性插值，返回 0.1g 单位的换算结果。 */
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

static uint32_t WeightCalibration_ApplyTableX10(int32_t raw_value,
                                                const WeightCalibrationPoint_t *points,
                                                uint8_t count)
{
  uint8_t i;
  uint8_t best_idx = 0U;
  uint32_t best_distance = 0xFFFFFFFFU;

  if (!WeightCalibration_IsValidTable(points, count)) {
    return CS1237_GetMeasurementX10();
  }

  for (i = 0U; i < (count - 1U); i++) {
    int32_t raw_a = points[i].raw_value;
    int32_t raw_b = points[i + 1U].raw_value;
    int32_t range_min = (raw_a < raw_b) ? raw_a : raw_b;
    int32_t range_max = (raw_a > raw_b) ? raw_a : raw_b;

    if ((raw_value >= range_min) && (raw_value <= range_max)) {
      return WeightCalibration_InterpolateSegmentX10(raw_value,
                                                     raw_a,
                                                     raw_b,
                                                     points[i].real_x10,
                                                     points[i + 1U].real_x10);
    }
  }

  for (i = 0U; i < (count - 1U); i++) {
    int32_t raw_a = points[i].raw_value;
    int32_t raw_b = points[i + 1U].raw_value;
    uint32_t distance_a = (raw_value > raw_a) ? (uint32_t)(raw_value - raw_a) : (uint32_t)(raw_a - raw_value);
    uint32_t distance_b = (raw_value > raw_b) ? (uint32_t)(raw_value - raw_b) : (uint32_t)(raw_b - raw_value);
    uint32_t segment_distance = (distance_a < distance_b) ? distance_a : distance_b;

    if (segment_distance < best_distance) {
      best_distance = segment_distance;
      best_idx = i;
    }
  }

  return WeightCalibration_InterpolateSegmentX10(raw_value,
                                                 points[best_idx].raw_value,
                                                 points[best_idx + 1U].raw_value,
                                                 points[best_idx].real_x10,
                                                 points[best_idx + 1U].real_x10);
}

void WeightCalibration_LoadRuntimeFromFlash(void)
{
#if (WEIGHT_CALIBRATION_RUNTIME_ENABLE != 0U)
  const WeightCalibrationFlash_t *store_data =
      (const WeightCalibrationFlash_t *)WEIGHT_CALIBRATION_FLASH_ADDR;

  g_runtime_point_count = 0U;

  if (store_data->magic != WEIGHT_CALIBRATION_FLASH_MAGIC) {
    return;
  }
  if (store_data->version != WEIGHT_CALIBRATION_FLASH_VERSION) {
    return;
  }
  if (store_data->check != WeightCalibration_MakeCheck(store_data)) {
    return;
  }
  if (!WeightCalibration_IsValidTable(store_data->points, (uint8_t)store_data->point_count)) {
    return;
  }

  WeightCalibration_CopyRuntimeTable(store_data->points, (uint8_t)store_data->point_count);
#endif
}

uint8_t WeightCalibration_SaveRuntimeTable(const WeightCalibrationPoint_t *points, uint8_t count)
{
#if (WEIGHT_CALIBRATION_RUNTIME_ENABLE != 0U)
  FLASH_EraseInitTypeDef erase_init;
  uint32_t page_error = 0U;
  WeightCalibrationFlash_t store_data;
  const uint32_t *write_words = (const uint32_t *)&store_data;
  uint32_t word_index;
  HAL_StatusTypeDef hal_ret;

  if (!WeightCalibration_IsValidTable(points, count)) {
    return 0U;
  }

  store_data.magic = WEIGHT_CALIBRATION_FLASH_MAGIC;
  store_data.version = WEIGHT_CALIBRATION_FLASH_VERSION;
  store_data.point_count = count;
  for (word_index = 0U; word_index < WEIGHT_CALIBRATION_MAX_POINTS; word_index++) {
    store_data.points[word_index].raw_value = 0;
    store_data.points[word_index].real_x10 = 0U;
  }
  for (word_index = 0U; word_index < count; word_index++) {
    store_data.points[word_index] = points[word_index];
  }
  store_data.check = WeightCalibration_MakeCheck(&store_data);

  HAL_FLASH_Unlock();

  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.PageAddress = WEIGHT_CALIBRATION_FLASH_ADDR;
  erase_init.NbPages = 1U;

  hal_ret = HAL_FLASHEx_Erase(&erase_init, &page_error);
  if (hal_ret != HAL_OK) {
    HAL_FLASH_Lock();
    return 0U;
  }

  for (word_index = 0U; word_index < (sizeof(store_data) / sizeof(uint32_t)); word_index++) {
    hal_ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                WEIGHT_CALIBRATION_FLASH_ADDR + (word_index * 4U),
                                write_words[word_index]);
    if (hal_ret != HAL_OK) {
      HAL_FLASH_Lock();
      return 0U;
    }
  }

  HAL_FLASH_Lock();
  WeightCalibration_CopyRuntimeTable(points, count);
  return 1U;
#else
  (void)points;
  (void)count;
  return 0U;
#endif
}

uint8_t WeightCalibration_ClearRuntimeTable(void)
{
#if (WEIGHT_CALIBRATION_RUNTIME_ENABLE != 0U)
  FLASH_EraseInitTypeDef erase_init;
  uint32_t page_error = 0U;
  HAL_StatusTypeDef hal_ret;

  g_runtime_point_count = 0U;

  HAL_FLASH_Unlock();
  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.PageAddress = WEIGHT_CALIBRATION_FLASH_ADDR;
  erase_init.NbPages = 1U;
  hal_ret = HAL_FLASHEx_Erase(&erase_init, &page_error);
  HAL_FLASH_Lock();

  return (hal_ret == HAL_OK) ? 1U : 0U;
#else
  return 0U;
#endif
}

uint8_t WeightCalibration_HasRuntimeTable(void)
{
  return (g_runtime_point_count >= 2U) ? 1U : 0U;
}

uint8_t WeightCalibration_GetRuntimePointCount(void)
{
  return g_runtime_point_count;
}

/* 将原始码值映射到最近的分段区间，并输出换算后的重量。 */
uint32_t WeightCalibration_ApplySegmentCalibrationX10(int32_t raw_value)
{
#if (CS1237_SEGMENT_CAL_ENABLE != 0U)
  static const WeightCalibrationPoint_t default_points[CS1237_CAL_POINT_COUNT] = {
      {CS1237_CAL0_RAW, CS1237_CAL0_REAL_X10},
      {CS1237_CAL1_RAW, CS1237_CAL1_REAL_X10},
      {CS1237_CAL2_RAW, CS1237_CAL2_REAL_X10},
      {CS1237_CAL3_RAW, CS1237_CAL3_REAL_X10},
      {CS1237_CAL4_RAW, CS1237_CAL4_REAL_X10},
      {CS1237_CAL5_RAW, CS1237_CAL5_REAL_X10}
  };

  /* 上位机写入的标定表优先级最高；未写入或校验失败时才使用编译期默认表。 */
  if (WeightCalibration_HasRuntimeTable() != 0U) {
    return WeightCalibration_ApplyTableX10(raw_value, g_runtime_points, g_runtime_point_count);
  }

  return WeightCalibration_ApplyTableX10(raw_value, default_points, CS1237_CAL_POINT_COUNT);
#else
  return CS1237_GetMeasurementX10();
#endif
}
